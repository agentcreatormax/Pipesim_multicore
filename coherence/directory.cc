#include "pa4.h"

namespace multicore {
namespace {

bool acquisition(MessageType type) {
    return type == MessageType::GetS || type == MessageType::GetM;
}

bool release(MessageType type) {
    return type == MessageType::PutS || type == MessageType::PutM;
}

void start(MulticoreSystem& system, uint32_t line, DirectoryEntry& entry, const Packet& request) {
    entry.active = true;
    entry.request = request;
    entry.phase = "start";
    entry.started = system.cycle;
    entry.pending_acknowledgements.clear();
    entry.unsent_invalidations.clear();
    system.event("directory." + std::to_string(system.home(line) - system.configuration.cores),
                 "transactions_started");
}

void complete(MulticoreSystem& system, uint32_t line, DirectoryEntry& entry) {
    const auto prefix = "directory." + std::to_string(system.home(line) - system.configuration.cores);
    system.event(prefix, "transactions_completed");
    system.event(prefix, "completed_latency_sum", system.cycle + 1 - entry.started);
    entry.active = false;
    entry.phase = "idle";
    entry.pending_acknowledgements.clear();
    entry.unsent_invalidations.clear();
}

LLCRequest llc_operation(uint32_t line, const DirectoryEntry& entry, bool write) {
    LLCRequest request;
    request.line_address = line;
    request.transaction_id = entry.request.transaction_id;
    request.requester = entry.request.requester;
    request.write = write;
    request.data = entry.data;
    return request;
}

} 

bool handle_directory_message(MulticoreSystem& system, const Packet& packet) {
    const uint32_t line = packet.line_address;
    const int home = system.home(line);
    const auto prefix = "directory." + std::to_string(home - system.configuration.cores);
    auto& entry = system.next.directory[line];

    if (acquisition(packet.type) || release(packet.type)) {
        if (entry.active) {
            if (entry.waiting.size() >= system.configuration.router_depth) return false;
            entry.waiting.push_back(packet);
            system.event(prefix, "requests_accepted");
            system.event(prefix, "conflicts_queued");
        } else if (!entry.waiting.empty()) {
            // Requests already queued on this line must not be overtaken.
            if (entry.waiting.size() >= system.configuration.router_depth) return false;
            entry.waiting.push_back(packet);
            system.event(prefix, "requests_accepted");
            system.event(prefix, "conflicts_queued");
        } else {
            start(system, line, entry, packet);
            system.event(prefix, "requests_accepted");
        }
        return true;
    }

    // Snoops/GrantAck reply to the *active* transaction, never the waiting
    // queue. Match transaction (not transport packet) ID and original requester.
    if (!entry.active || packet.transaction_id != entry.request.transaction_id ||
        packet.requester != entry.request.requester) {
        throw std::runtime_error("Unmatched directory protocol reply");
    }

    if (packet.type == MessageType::Ack) {
        if ((entry.phase != "await_recall" && entry.phase != "send_invs" &&
             entry.phase != "await_invs") ||
            !entry.pending_acknowledgements.count(packet.source))
            throw std::runtime_error("Unexpected snoop acknowledgement");
        entry.pending_acknowledgements.erase(packet.source);
        system.event(prefix, "acks_received");
        if (entry.phase == "await_recall") {
            entry.data = packet.data;
            entry.phase = "llc_write";
        } else if (entry.phase == "await_invs" && entry.pending_acknowledgements.empty()) {
            entry.phase = "llc_read";
        }
        return true;
    }

    if (packet.type == MessageType::GrantAck) {
        if (entry.phase != "await_grant" || packet.source != entry.request.source)
            throw std::runtime_error("Unexpected GrantAck");
        if (entry.request.type == MessageType::GetM) {
            entry.state = CacheState::M;
            entry.owner = entry.request.source;
            entry.sharers.clear();
        } else if (entry.request.type == MessageType::GetS) {
            // A dirty owner was recalled into S before the new copy was sent.
            if (entry.state == CacheState::M && entry.owner >= 0)
                entry.sharers.insert(entry.owner);
            entry.sharers.insert(entry.request.source);
            entry.owner = -1;
            entry.state = CacheState::S;
        } else {
            throw std::runtime_error("GrantAck on nonacquisition");
        }
        complete(system, line, entry);
        return true;
    }
    throw std::runtime_error("Unexpected message at directory");
}

void advance_directory_transaction(MulticoreSystem& system, uint32_t line) {
    auto it = system.next.directory.find(line);
    if (it == system.next.directory.end()) return;
    auto& entry = it->second;
    const int home = system.home(line);
    const auto prefix = "directory." + std::to_string(home - system.configuration.cores);

    if (!entry.active) {
        if (entry.waiting.empty()) return;
        auto request = entry.waiting.front();
        entry.waiting.pop_front();
        start(system, line, entry, request);
        return;
    }
    // Newly started transactions may advance only on their NEXT cycle.
    if (entry.started == system.cycle) return;

    const auto& request = entry.request;
    if (entry.phase == "start") {
        if (request.type == MessageType::PutS) {
            entry.phase = "put_ack";
        } else if (request.type == MessageType::PutM) {
            // A PutM may have sat in the queue while a RecallI/S returned
            // the same victim. Only the CURRENT owner may install dirty data.
            entry.phase = entry.state == CacheState::M && entry.owner == request.source
                ? "put_write" : "put_ack";
        } else if (request.type == MessageType::GetS) {
            if (entry.state == CacheState::M && entry.owner != request.source) {
                entry.phase = "send_recall";
            } else {
                entry.phase = "llc_read";
            }
        } else if (request.type == MessageType::GetM) {
            if (entry.state == CacheState::M && entry.owner != request.source) {
                entry.phase = "send_recall";
            } else if (entry.state == CacheState::S) {
                entry.pending_acknowledgements = entry.sharers;
                entry.pending_acknowledgements.erase(request.source);
                entry.unsent_invalidations = entry.pending_acknowledgements;
                entry.phase = entry.unsent_invalidations.empty() ? "llc_read" : "send_invs";
            } else {
                entry.phase = "llc_read";
            }
        }
        return;
    }

    if (entry.phase == "send_recall") {
        const auto type = request.type == MessageType::GetM ? MessageType::RecallI : MessageType::RecallS;
        if (!system.can_send(home, type)) return;
        if (!system.send(system.message(type, home, entry.owner, line,
                                        request.transaction_id, {}, request.requester))) return;
        entry.pending_acknowledgements.insert(entry.owner);
        entry.phase = "await_recall";
        system.event(prefix, "recalls_sent");
        return;
    }

    if (entry.phase == "send_invs") {
        if (!entry.unsent_invalidations.empty()) {
            const int target = *entry.unsent_invalidations.begin();
            if (!system.can_send(home, MessageType::Inv)) return;
            if (!system.send(system.message(MessageType::Inv, home, target, line,
                                            request.transaction_id, {}, request.requester))) return;
            entry.unsent_invalidations.erase(target);
            system.event(prefix, "invalidations_sent");
        }
        if (entry.unsent_invalidations.empty())
            entry.phase = entry.pending_acknowledgements.empty() ? "llc_read" : "await_invs";
        return;
    }

    if (entry.phase == "await_invs") {
        if (entry.pending_acknowledgements.empty()) entry.phase = "llc_read";
        return;
    }

    if (entry.phase == "llc_read" || entry.phase == "llc_write" || entry.phase == "put_write") {
        const bool write = entry.phase != "llc_read";
        auto operation = llc_operation(line, entry, write);
        if (entry.phase == "put_write") operation.data = request.data;
        if (!system.llc_request(home, operation)) return;
        if (entry.phase == "put_write") entry.data = request.data;
        entry.phase = entry.phase == "put_write" ? "put_wait" :
                      (write ? "llc_write_wait" : "llc_read_wait");
        return;
    }

    if (entry.phase == "llc_read_wait" || entry.phase == "llc_write_wait" ||
        entry.phase == "put_wait") {
        const auto reply = system.llc_reply(home, line, request.transaction_id, request.requester);
        if (!reply) return;
        if (entry.phase == "put_wait") {
            entry.phase = "put_ack";
        } else {
            entry.data = reply->data;
            entry.phase = "grant";
        }
        return;
    }

    if (entry.phase == "grant") {
        const auto type = request.type == MessageType::GetM ? MessageType::DataM : MessageType::DataS;
        if (!system.can_send(home, type)) return;
        if (!system.send(system.message(type, home, request.source, line,
                                        request.transaction_id, entry.data, request.requester))) return;
        entry.phase = "await_grant";
        if (type == MessageType::DataM) system.event(prefix, "ownership_grants");
        return;
    }

    if (entry.phase == "put_ack") {
        if (!system.can_send(home, MessageType::PutAck)) return;
        if (!system.send(system.message(MessageType::PutAck, home, request.source, line,
                                        request.transaction_id, {}, request.requester))) return;
        if (request.type == MessageType::PutS) {
            entry.sharers.erase(request.source);
            if (entry.state == CacheState::S && entry.sharers.empty()) entry.state = CacheState::I;
        } else if (request.type == MessageType::PutM) {
            if (entry.state == CacheState::M && entry.owner == request.source) {
                entry.state = CacheState::I;
                entry.owner = -1;
            } else if (entry.state == CacheState::S) {
                // The old dirty owner may have already answered RecallS.
                // Its queued PutM must still release the stale sharer.
                entry.sharers.erase(request.source);
                if (entry.sharers.empty()) entry.state = CacheState::I;
            }
        }
        complete(system, line, entry);
        return;
    }
}

} 
