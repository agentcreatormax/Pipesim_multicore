#include "pa4.h"
#include <algorithm>

namespace multicore {

    bool MulticoreSystem::llc_request(int home_tile, const LLCRequest& request) {

        const int index = home_tile - configuration.cores;

        if (index < 0 || index >= configuration.homes || home(request.line_address) != home_tile) {

            throw std::runtime_error("LLC request sent to the wrong bank");

        }

        auto& next_bank = next.llc[index];

        if (next_bank.requests.size() >= configuration.llc_queue_entries) {

            return false;

        }

        auto accepted = request;

        accepted.accepted = cycle;

        next_bank.requests.push_back(accepted);

        observer.add("llc.accepted");

        event("llc." + std::to_string(index), "requests_accepted");

        return true;

    }

    std::optional<LLCReply> MulticoreSystem::llc_reply(     int home_tile, uint32_t line, uint64_t transaction_id, int requester) {

        const int index = home_tile - configuration.cores;

        const auto& replies = current.llc.at(index).replies;

        for (const auto& reply : replies) {

            if (reply.line_address != line || reply.transaction_id != transaction_id || reply.requester != requester) {

                continue;

            }

            auto& staged = next.llc[index].replies;

            auto match = std::find_if(staged.begin(), staged.end(), [&](const LLCReply& value) {

                return value.line_address == line && value.transaction_id == transaction_id && value.requester == requester;
            });

            if (match == staged.end()) {

                return {

                };

            }

            staged.erase(match);

            return reply;

        }

        return {

        };

    }

    bool MulticoreSystem::llc_memory_reply(const Packet& packet) {

        if (packet.destination < configuration.cores || packet.destination >= configuration.cores + configuration.homes) {

            return false;

        }

        const int index = packet.destination - configuration.cores;

        const auto& current_bank = current.llc[index];

        auto& next_bank = next.llc[index];

        if (!current_bank.active || current_bank.memory_transaction_id != packet.transaction_id) {

            throw std::runtime_error("DRAM reply has no LLC transaction owner");

        }

        if (current_bank.phase == "writeback" && packet.type == MessageType::MemAck) {

            next_bank.phase = current_bank.active->write ? "install" : "fetch";

            next_bank.entries[current_bank.slot].valid = false;

            return true;

        }

        if (current_bank.phase == "fill" && packet.type == MessageType::MemData) {

            auto& entry = next_bank.entries[current_bank.slot];

            entry = {

                true, false, current_bank.active->line_address, packet.data
            };

            next_bank.phase = "reply";

            next_bank.ready = cycle + configuration.llc_latency;

            return true;

        }

        throw std::runtime_error("Unexpected LLC memory response");

    }

    void MulticoreSystem::llc() {

        for (int index = 0; index < configuration.homes; ++index) {

            const int home_tile = configuration.cores + index;

            const auto& current_bank = current.llc[index];

            auto& next_bank = next.llc[index];

            const std::string prefix = "llc." + std::to_string(index);

            if (!current_bank.active) {

                if (current_bank.requests.empty()) {

                    continue;

                }

                next_bank.active = current_bank.requests.front();

                next_bank.requests.pop_front();

                next_bank.slot = (current_bank.requests.front().line_address / 64 / configuration.homes) % configuration.llc_lines;

                next_bank.phase = "lookup";

                next_bank.ready = cycle + configuration.llc_latency;

                continue;

            }

            if (cycle < current_bank.ready) {

                continue;

            }

            const auto& request = *current_bank.active;

            const auto& victim = current_bank.entries[current_bank.slot];

            if (current_bank.phase == "lookup") {

                const bool hit = victim.valid && victim.tag == request.line_address;

                observer.add(hit ? "llc.hits" : "llc.misses");

                event(prefix, hit ? "hits" : "misses");

                if (hit) {

                    if (request.write) {

                        next_bank.entries[current_bank.slot].data = request.data;

                        next_bank.entries[current_bank.slot].dirty = true;

                    }

                    next_bank.phase = "reply";

                }

                else if (victim.valid && victim.dirty) {

                    auto packet = message(MessageType::MemWrite, home_tile, controller(victim.tag),                                       victim.tag, memory_next, victim.data, request.requester);

                    if (!send(packet)) {

                        // Classification is a successful lookup decision, not repeated each retry.
                        next_bank.phase = "evict";

                        continue;

                    }

                    next_bank.memory_transaction_id = memory_next++;

                    next_bank.phase = "writeback";

                    event(prefix, "dirty_evictions");

                }

                else {

                    next_bank.phase = request.write ? "install" : "fetch";

                }

            }

            else if (current_bank.phase == "evict") {

                auto packet = message(MessageType::MemWrite, home_tile, controller(victim.tag),                                   victim.tag, memory_next, victim.data, request.requester);

                if (send(packet)) {

                    next_bank.memory_transaction_id = memory_next++;

                    next_bank.phase = "writeback";

                    event(prefix, "dirty_evictions");

                }

            }

            else if (current_bank.phase == "fetch") {

                if (send(message(MessageType::MemRead, home_tile, controller(request.line_address),                              request.line_address, memory_next, {}, request.requester))) {

                    next_bank.memory_transaction_id = memory_next++;

                    next_bank.phase = "fill";

                }

            }

            else if (current_bank.phase == "install") {

                next_bank.entries[current_bank.slot] = {

                    true, true, request.line_address, request.data
                };

                next_bank.phase = "reply";

                next_bank.ready = cycle + configuration.llc_latency;

            }

            else if (current_bank.phase == "reply") {

                if (next_bank.replies.size() >= configuration.llc_queue_entries) {

                    continue;

                }

                next_bank.replies.push_back(                 {

                    request.line_address, request.transaction_id, request.requester, next_bank.entries[current_bank.slot].data
                });

                next_bank.active.reset();

                next_bank.phase = "idle";

                observer.add("llc.completed");

                event(prefix, "requests_completed");

            }

        }

    }

}

