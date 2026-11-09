#include "pa4.h"

namespace multicore {
namespace {

unsigned word_index(uint32_t address) { return (address % 64) / 4; }

MessageType acquisition(const MemoryRequest& request) {
    return request.operation == MemoryOperation::Load ? MessageType::GetS : MessageType::GetM;
}

uint32_t apply_access(CacheLine& line, const MemoryRequest& request) {
    auto& word = line.data[word_index(request.address)];
    switch (request.operation) {
        case MemoryOperation::Load: return word;
        case MemoryOperation::Store: word = request.value; return 0;
        case MemoryOperation::Exchange: {
            const uint32_t previous = word;
            word = request.value;
            return previous;
        }
        case MemoryOperation::Fence: break; // Ordering completes fences, not cache.
    }
    throw std::runtime_error("FENCE forwarded to cache");
}

void count_accepted(MulticoreSystem& system, int core, const MemoryRequest& req,
                    bool permission_hit, bool upgrade) {
    const auto prefix = "cache." + std::to_string(core);
    if (req.operation == MemoryOperation::Load) {
        system.event(prefix, "loads_accepted");
        system.event(prefix, permission_hit ? "load_permission_hits" : "read_misses");
    } else if (req.operation == MemoryOperation::Store) {
        system.event(prefix, "stores_accepted");
        if (permission_hit) system.event(prefix, "store_permission_hits");
        else if (upgrade) system.event(prefix, "upgrades");
        else system.event(prefix, "write_misses");
    } else if (req.operation == MemoryOperation::Exchange) {
        system.event(prefix, "atomics_accepted");
        system.event(prefix, permission_hit ? "atomic_permission_hits" : "atomic_misses");
    }
}

} 

bool accept_cache_request(MulticoreSystem& system, int core, const MemoryRequest& request) {
    auto& cache = system.next.caches.at(core);
    if (cache.pending || request.operation == MemoryOperation::Fence) return false;
    const uint32_t tag = system.line_address(request.address);
    auto& line = cache.lines.at(system.slot(request.address));
    const bool match = line.state != CacheState::I && line.tag == tag;
    const bool hit = match && (request.operation == MemoryOperation::Load ||
                                line.state == CacheState::M);
    const bool upgrade = match && line.state == CacheState::S &&
                         request.operation == MemoryOperation::Store;

    if (hit) {
        cache.pending = request;
        cache.transaction_id = request.id;
        count_accepted(system, core, request, true, false);
        const uint32_t value = apply_access(line, request);
        system.cache_complete(core, value);
        return true;
    }

    // A live conflicting direct-mapped slot must be released before fetching
    // the new line. Even clean victims require PutAck: the directory must not
    // retain a stale sharer in the presence of future writers.
    const bool victim = line.state != CacheState::I && line.tag != tag;
    const MessageType send_type = victim
        ? (line.state == CacheState::M ? MessageType::PutM : MessageType::PutS)
        : acquisition(request);
    if (!system.can_send(core, send_type)) return false;
    const uint32_t send_tag = victim ? line.tag : tag;
    const CacheLineData payload = victim && line.state == CacheState::M ? line.data : CacheLineData{};
    if (!system.send(system.message(send_type, core, system.home(send_tag),
                                    send_tag, request.id, payload, core))) return false;

    cache.pending = request;
    cache.transaction_id = request.id;
    count_accepted(system, core, request, false, upgrade);
    line.transient = true;
    if (victim) {
        line.state = CacheState::I;
        cache.phase = "evict_wait";
        cache.evicted_line_address = send_tag;
        if (send_type == MessageType::PutM) {
            system.event("cache." + std::to_string(core), "dirty_evictions");
            system.event("cache." + std::to_string(core), "writebacks_sent");
        }
    } else {
        cache.phase = "grant_wait";
        // For a completely invalid slot, track the new tag while waiting.
        if (!match) { line.tag = tag; line.state = CacheState::I; }
    }
    return true;
}

bool handle_cache_message(MulticoreSystem& system, int core, const Packet& packet) {
    auto& cache = system.next.caches.at(core);
    const auto prefix = "cache." + std::to_string(core);
    auto& line = cache.lines.at(system.slot(packet.line_address));
    const bool same_line = line.tag == packet.line_address;
    const bool valid = same_line && line.state != CacheState::I;
    const bool retained_victim = same_line && cache.phase == "evict_wait" &&
                                 cache.evicted_line_address &&
                                 *cache.evicted_line_address == packet.line_address;

    if (packet.type == MessageType::Inv || packet.type == MessageType::RecallS ||
        packet.type == MessageType::RecallI) {
        if (!system.can_send(core, MessageType::Ack)) return false;
        // A pending PutM retains its victim until PutAck; a recall can race
        // with the queued eviction. The retained data is still authoritative.
        const CacheLineData data = (valid || retained_victim) ? line.data : CacheLineData{};
        if (!system.send(system.message(MessageType::Ack, core, packet.source,
                                        packet.line_address, packet.transaction_id,
                                        data, packet.requester))) return false;
        if (packet.type == MessageType::Inv) system.event(prefix, "invalidations_received");
        if (valid || retained_victim) {
            if (packet.type != MessageType::RecallS || retained_victim) {
                 line.state = CacheState::I;
                 if (packet.type == MessageType::Inv)
                     system.event(prefix, "invalidations_applied");
            } else {
                line.state = CacheState::S;
            }
        }
        // Do not cancel a GetM upgrade or victim eviction if snooped.
        system.event(prefix, "acks_sent");
        return true;
    }

    if (packet.type == MessageType::PutAck) {
        if (!cache.pending || cache.phase != "evict_wait" ||
            !cache.evicted_line_address || *cache.evicted_line_address != packet.line_address ||
            packet.transaction_id != cache.transaction_id || packet.requester != core)
            throw std::runtime_error("Unexpected PutAck at private cache");
        const auto& request = *cache.pending;
        const uint32_t target = system.line_address(request.address);
        const auto want = acquisition(request);
        // Transactional consumption: cannot discard the retained victim or
        // consume PutAck until injection of the next GetS/GetM can succeed.
        if (!system.can_send(core, want)) return false;
        if (!system.send(system.message(want, core, system.home(target),
                                        target, request.id, {}, core))) return false;
        auto& target_line = cache.lines.at(system.slot(target));
        target_line.state = CacheState::I;
        target_line.tag = target;
        target_line.transient = true;
        cache.evicted_line_address.reset();
        cache.phase = "grant_wait";
        return true;
    }

    if (packet.type == MessageType::DataS || packet.type == MessageType::DataM) {
        if (!cache.pending || cache.phase != "grant_wait" ||
            packet.line_address != system.line_address(cache.pending->address) ||
            packet.transaction_id != cache.transaction_id || packet.requester != core ||
            (packet.type == MessageType::DataS && cache.pending->operation != MemoryOperation::Load) ||
            (packet.type == MessageType::DataM && cache.pending->operation == MemoryOperation::Load))
            throw std::runtime_error("Unexpected or mismatched coherence grant");
        if (!system.can_send(core, MessageType::GrantAck)) return false;
        if (!system.send(system.message(MessageType::GrantAck, core, packet.source,
                                        packet.line_address, packet.transaction_id, {}, core))) return false;
        auto& granted = cache.lines.at(system.slot(packet.line_address));
        granted.tag = packet.line_address;
        granted.data = packet.data;
        granted.state = packet.type == MessageType::DataM ? CacheState::M : CacheState::S;
        granted.transient = false;
        const auto request = *cache.pending;
        const uint32_t value = apply_access(granted, request);
        system.cache_complete(core, value);
        return true;
    }
    throw std::runtime_error("Unexpected coherence message at private cache");
}

}
