#include "pa4.h"
#include <cassert>
#include <iostream>

// Scripted fixtures for supplied machinery, not implementations of routing, coherence, or ordering policies.
namespace multicore {

    int select_output_port(const RoutingContext& view, const SystemConfig&) {

        if (view.current_router.x == view.destination_router.x && view.current_router.y == view.destination_router.y) return Local;

        if (view.destination_router.x == 2 && view.destination_router.y == 2) {

            switch (view.router_tile_id) {

                case 0: case 1: return PositiveX;

                case 2: case 5: return PositiveY;

            }

        }

        if (view.router_tile_id == 1 && view.destination_router.x == 2 && view.destination_router.y == 0) return PositiveX;

        if (view.router_tile_id == 2 && view.destination_router.x == 1 && view.destination_router.y == 0) return NegativeX;

        throw std::runtime_error("Unexpected fixture path");

    }

    std::optional<unsigned> select_router_input(     const std::vector<ArbitrationCandidate>& offers, unsigned, const SystemConfig&) {

        if (offers.size() != 1) throw std::runtime_error("Fixture expects one contender");

        return offers.front().input;

    }

    MemoryOrderDecision decide_memory_action(const MemoryRequest&, const MemoryOrderUnit&, const SystemConfig&) {

        throw std::runtime_error("Ordering not used by fixtures");

    }

    std::optional<unsigned> select_memory_request(     const std::vector<unsigned>& legal, const MemoryController&, Cycle, const SystemConfig&) {

        if (legal.size() != 1) throw std::runtime_error("Fixture expects one legal request");

        return legal.front();

    }

    bool accept_cache_request(MulticoreSystem&, int, const MemoryRequest&) {

        throw std::runtime_error("Not used");

    }

    bool handle_cache_message(MulticoreSystem&, int, const Packet&) {

        throw std::runtime_error("Not used");

    }

    bool handle_directory_message(MulticoreSystem&, const Packet&) {

        throw std::runtime_error("Not used");

    }

    void advance_directory_transaction(MulticoreSystem&, uint32_t) {

        throw std::runtime_error("Not used");

    }

    void count_event(MulticoreSystem&, const std::string&, const std::string&, uint64_t) {

    }

    void sample_occupancy_and_stalls(MulticoreSystem&) {

    }

}

int main() {

    using namespace multicore;

    SystemConfig config;

    config.cores = config.homes = config.controllers = 1;

    config.llc_lines = 1;

    MulticoreSystem system(config);

    system.begin();

    assert(system.send(system.message(MessageType::GetS, 0, 8, 0, 1)));

    system.evaluate();

    system.commit();

    for (unsigned n = 0; n < 100 && !system.observer.get("noc.delivered"); ++n) system.tick();

    assert(system.observer.get("noc.hops") == 4);

    assert(system.observer.get("noc.link_bytes") == 64);

    assert(system.current.network.inbox[8].size() == 1);

    system.reset();

    assert(system.idle());

    assert(system.observer.get("noc.injected") == 0);

    system.initialize(0, 42);

    const auto access = [&](uint32_t line, bool write, uint32_t value, uint64_t transaction_id) {

        CacheLineData data {

        };

        data[0] = value;

        system.begin();

        assert(system.llc_request(1, {

            line, transaction_id, 0, write, data, system.cycle
        }));

        system.evaluate();

        system.commit();

        for (unsigned n = 0; n < 1000; ++n) {

            system.begin();

            auto reply = system.llc_reply(1, line, transaction_id, 0);

            system.evaluate();

            system.commit();

            if (reply) return reply->data[0];

        }

        throw std::runtime_error("LLC fixture timed out");

    };

    assert(access(0, false, 0, 10) == 42);

    assert(access(0, false, 0, 11) == 42);

    assert(access(0, true, 99, 12) == 99);

    assert(access(64, false, 0, 13) == 0);

    assert(access(0, false, 0, 14) == 99);

    assert(system.observer.get("dram.WR") >= 1);

    for (unsigned n = 0; n < 500; ++n) system.tick();

    assert(system.observer.get("dram.REF") >= 1);

    std::cout << "PASS framework: hops, serialization, reset, LLC fills/hits, "                  "dirty eviction, DRAM persistence and refresh\n";

}

