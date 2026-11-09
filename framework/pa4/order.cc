#include "pa4.h"
#include <algorithm>

namespace multicore {

    void MulticoreSystem::ordering() {

        for (int core = 0; core < configuration.cores; ++core) {

            const auto& old = current.order[core];

            auto& unit = next.order[core];

            const std::string prefix = "order." + std::to_string(core);

            if (!current.cache_results[core].empty()) {

                if (!old.inflight) {

                    throw std::runtime_error("Cache reply without ordering owner");

                }

                const auto result = current.cache_results[core].front();

                if (result.id != old.inflight->id) {

                    throw std::runtime_error("Ordering response ID mismatch");

                }

                next.cache_results[core].pop_front();

                if (old.draining) {

                    if (unit.stores.empty() || unit.stores.front().id != result.id) {

                        throw std::runtime_error("FIFO drain mismatch");

                    }

                    unit.stores.pop_front();

                    observer.add("order.drained");

                    event(prefix, "stores_drained");

                }

                else {

                    cpu_complete(core, *old.inflight, result.value);

                }

                unit.inflight.reset();

                unit.draining = false;

            }

            if (old.inflight || current.cache_input[core]) {

                continue;

            }

            const auto drain = [&] {

                unit.inflight = old.stores.front();

                unit.draining = true;

                next.cache_input[core] = old.stores.front();

            };

            if (!current.cpu[core]) {

                // Four-cycle store-buffer admission-to-drain minimum. This gives
                // newly retired stores a modeled interval before their write port.
                if (!old.stores.empty() && cycle >= old.stores.front().accepted + 4) {

                    drain();

                }

                continue;

            }

            const auto request = *current.cpu[core];

            MemoryOrderDecision decision;

            if (configuration.memory_model == "SC") {

                decision.kind = request.operation == MemoryOperation::Fence                                 ? MemoryAction::CompleteFence                                 : MemoryAction::Issue;

            }

            else {

                decision = multicore::decide_memory_action(request, old, configuration);

            }

            if (decision.kind == MemoryAction::Wait) {

                if (!old.stores.empty()) {

                    drain();

                }

                continue;

            }

            if (decision.kind == MemoryAction::BufferStore) {

                if (request.operation != MemoryOperation::Store || unit.stores.size() >= configuration.store_entries) {

                    throw std::runtime_error("Illegal store buffering");

                }

                unit.stores.push_back(request);

                cpu_complete(core, request, 0);

                observer.add("order.enqueued");

                event(prefix, "stores_enqueued");

            }

            else if (decision.kind == MemoryAction::Forward) {

                if (request.operation != MemoryOperation::Load) {

                    throw std::runtime_error("Forwarding a nonload");

                }

                cpu_complete(core, request, decision.value);

                event(prefix, "loads_forwarded");
                observer.add("order.forwarded");

            }

            else if (decision.kind == MemoryAction::CompleteFence) {

                if (request.operation != MemoryOperation::Fence || !old.stores.empty()) {

                    throw std::runtime_error("Illegal fence completion");

                }

                cpu_complete(core, request, 0);

                event(prefix, "fences_completed");

            }

            else if (decision.kind == MemoryAction::Issue) {

                if (request.operation == MemoryOperation::Fence) {

                    throw std::runtime_error("Fence sent to cache");

                }

                unit.inflight = request;

                unit.draining = false;

                next.cache_input[core] = request;

            }

            if (request.operation == MemoryOperation::Load &&             std::any_of(old.stores.begin(), old.stores.end(), [&](const MemoryRequest& store) {

                return store.address != request.address;
            })) {

                event(prefix, "loads_bypassed");
                observer.add("order.bypassed");

            }

            next.cpu[core].reset();

        }

    }

    void MulticoreSystem::endpoints() {

        // Separate capacity by protocol class, and process responses before requests.
        // A full request class must never prevent acknowledgement delivery.
        const auto selected = [&](int endpoint) -> std::optional<Packet> {

            for (int priority : {

                2, 1, 0
            }) {

                for (const auto& packet : current.network.inbox[endpoint]) {

                    if (channel(packet.type) == priority) {

                        return packet;

                    }

                }

            }

            return {

            };

        };

        const auto consume = [&](int endpoint, uint64_t id) {

            auto& inbox = next.network.inbox[endpoint];

            auto found = std::find_if(inbox.begin(), inbox.end(),                                  [&](const Packet& packet) {

                return packet.id == id;
            });

            if (found == inbox.end()) {

                throw std::runtime_error("Duplicate endpoint consumption");

            }

            inbox.erase(found);

        };

        for (int core = 0; core < configuration.cores; ++core) {

            auto packet = selected(core);

            if (packet && multicore::handle_cache_message(*this, core, *packet)) {

                consume(core, packet->id);

            }

            if (current.cache_input[core] && !current.caches[core].pending &&             multicore::accept_cache_request(*this, core, *current.cache_input[core])) {

                next.cache_input[core].reset();

            }

        }

        for (int endpoint = configuration.cores; endpoint < configuration.cores + configuration.homes; ++endpoint) {

            auto packet = selected(endpoint);

            if (!packet) {

                continue;

            }

            bool accepted = false;

            if (packet->type == MessageType::MemData || packet->type == MessageType::MemAck) {

                accepted = llc_memory_reply(*packet);

            }

            else {

                if (home(packet->line_address) != endpoint) {

                    throw std::runtime_error("Coherence packet delivered to wrong home");

                }

                accepted = multicore::handle_directory_message(*this, *packet);

            }

            if (accepted) {

                consume(endpoint, packet->id);

            }

        }

        for (const auto& [line, directory] : current.directory) {

            (void)directory;

            multicore::advance_directory_transaction(*this, line);

        }

        // Independent observation of controller-owned directory transitions.
        for (const auto& [line_address, entry] : next.directory) {
            auto previous = current.directory.find(line_address);
            const bool was_active = previous != current.directory.end() && previous->second.active;
            const bool changed_request = was_active && entry.active &&
                previous->second.request.id != entry.request.id;
            if (was_active && (!entry.active || changed_request)) observer.add("directory.completed");
            if (entry.active && (!was_active || changed_request)) observer.add("directory.started");
        }

        for (int index = 0; index < configuration.controllers; ++index) {

            const int endpoint = configuration.cores + configuration.homes + index;

            auto packet = selected(endpoint);

            if (!packet) {

                continue;

            }

            if (packet->type != MessageType::MemRead && packet->type != MessageType::MemWrite) {

                throw std::runtime_error("Nonmemory packet at controller");

            }

            if (controller(packet->line_address) != endpoint) {

                throw std::runtime_error("Wrong memory-controller mapping");

            }

            if (next.controllers[index].queue.size() < configuration.mc_entries) {

                packet->controller_arrival = cycle;

                next.controllers[index].queue.push_back(*packet);

                consume(endpoint, packet->id);

                observer.add("mc.accepted");

                event("mc." + std::to_string(index), "requests_accepted");

            }

        }

    }

}

