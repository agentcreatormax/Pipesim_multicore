#include "pa4.h"
#include <algorithm>
namespace multicore {

    void MulticoreSystem::network() {

        // Arrivals are staged; they cannot be routed until the next common boundary.
        for(auto& transfer:current.network.flights)if(transfer.ready<=cycle) {

            auto protocol_class=channel(transfer.packet.type);

            auto input_index=transfer.input*3+protocol_class;

            auto packet=transfer.packet;

            packet.ready=cycle+configuration.router_latency;

            next.network.buffers[transfer.destination][input_index].push_back(packet);

            --next.network.reserved[transfer.destination][input_index];

            next.network.flights.erase(std::find_if(next.network.flights.begin(),next.network.flights.end(),[&](auto& transfer_candidate) {

                return transfer_candidate.packet.id==packet.id;
            }));

            observer.add("noc.hops");

            event("noc."+std::to_string(neighbor(transfer.destination,transfer.input)),"link_hops");

            log("arrive "+std::to_string(packet.id)+" "+std::to_string(transfer.destination));

            /* PA4-COUNT: accepted hop here */

        }

        for(int router_tile=0;router_tile<configuration.tiles();++router_tile) {

            bool downstream_blocked = false;

            for (int priority : {
                2, 1, 0
            }) {

                bool injected = false;

                for (const auto& offer : current.network.inject[router_tile]) {

                    if (channel(offer.type) != priority) continue;

                    const int input = Local * 3 + priority;

                    if (next.network.buffers[router_tile][input].size() + next.network.reserved[router_tile][input]                         >= configuration.router_depth) continue;

                    auto packet = offer;

                    packet.ready = cycle + configuration.router_latency;

                    next.network.buffers[router_tile][input].push_back(packet);

                    auto& queue = next.network.inject[router_tile];

                    queue.erase(std::find_if(queue.begin(), queue.end(), [&](const Packet& value) {

                        return value.id == packet.id;
                    }));

                    injected = true;

                    break;

                }

                if (injected) break;

            }

            std::array<bool,15> used {

            };

            for(int output=0;output<5;++output) {

                std::vector<ArbitrationCandidate> candidates;

                for(unsigned input=0;input<15;++input) {

                    if(used[input]||current.network.buffers[router_tile][input].empty())continue;

                    auto packet=current.network.buffers[router_tile][input].front();

                    if(packet.ready>cycle)continue;

                    int port=multicore::select_output_port( {

                        coord(router_tile),coord(packet.destination),unsigned(router_tile)
                    },configuration);

                    if(port<0||port>Local||neighbor(router_tile,port)<0)throw std::runtime_error("routing selected absent/invalid port");

                    if((port==Local)!=(packet.destination==router_tile))throw std::runtime_error("invalid local routing");

                    if(port!=output)continue;

                    bool can_forward=false;

                    int destination=neighbor(router_tile,port),input_index=(port^1)*3+channel(packet.type);

                    if(port==Local)can_forward=std::count_if(next.network.inbox[router_tile].begin(),next.network.inbox[router_tile].end(),[&](const Packet& queued_packet) {

                        return channel(queued_packet.type)==channel(packet.type);
                    })<configuration.router_depth;

                    else can_forward=current.network.link_ready[router_tile][port]<=cycle && next.network.buffers[destination][input_index].size()+next.network.reserved[destination][input_index]<configuration.router_depth;

                    if(can_forward)candidates.push_back( {

                        input,packet
                    });

                    else if(output==Local||current.network.link_ready[router_tile][output]<=cycle)downstream_blocked = true;

                    // PA4-COUNT: downstream blockage for eligible denied input
                }

                if(candidates.empty())continue;

                auto choice=multicore::select_router_input(candidates,current.network.round_robin_cursor[router_tile][output],configuration);

                if(!choice)continue;

                auto selected_candidate=std::find_if(candidates.begin(),candidates.end(),[&](auto& candidate) {

                    return candidate.input==*choice;
                });

                if(selected_candidate==candidates.end())throw std::runtime_error("arbiter selected noncandidate");

                auto packet=selected_candidate->packet;

                used[*choice]=true;

                next.network.buffers[router_tile][*choice].pop_front();

                next.network.round_robin_cursor[router_tile][output]=(*choice+1)%15;

                observer.add("noc.grants");

                event("noc."+std::to_string(router_tile),"grants");

                event("noc."+std::to_string(router_tile)+".port."+std::to_string(output),"grants");

                event("noc."+std::to_string(router_tile),"arbitration_losses",candidates.size()-1);

                // PA4-COUNT: grant and other eligible contenders' losses
                if(output==Local) {

                    next.network.inbox[router_tile].push_back(packet);

                    observer.add("noc.delivered");

                    event("noc."+std::to_string(router_tile),"packets_delivered");

                    event("noc."+std::to_string(router_tile),"delivered_"+std::string(name(packet.type)));

                    event("noc."+std::to_string(router_tile),"delivered_latency_sum",cycle+1-packet.injected);

                    counters.maximum("noc."+std::to_string(router_tile)+".delivered_latency_max",cycle+1-packet.injected);

                    observer.add(std::string("noc.delivered.")+name(packet.type));

                    observer.add("noc.latency_sum",cycle+1-packet.injected);

                    log("deliver "+std::to_string(packet.id)+" "+std::to_string(router_tile));

                    /* PA4-COUNT: delivery and latency */

                }

                else {

                    bool data=packet.type==MessageType::PutM||packet.type==MessageType::Ack||packet.type==MessageType::DataS||packet.type==MessageType::DataM||packet.type==MessageType::MemData||packet.type==MessageType::MemWrite;

                    unsigned bytes=16+(data?64:0);

                    Cycle serialization=(bytes+configuration.link_bytes-1)/configuration.link_bytes;

                    int destination=neighbor(router_tile,output),input_index=(output^1)*3+channel(packet.type);

                    next.network.reserved[destination][input_index]++;

                    next.network.link_ready[router_tile][output]=cycle+serialization;

                    next.network.flights.push_back( {

                        packet,destination,output^1,cycle+serialization+configuration.link_latency
                    });

                    observer.add("noc.link_bytes",bytes);

                    event("noc."+std::to_string(router_tile),"link_bytes",bytes);

                    log("link "+std::to_string(packet.id)+" "+std::to_string(router_tile)+" "+std::to_string(destination)+" "+std::to_string(bytes));

                    /* PA4-COUNT: bytes transmitted */

                }

            }
            if (downstream_blocked) event("noc."+std::to_string(router_tile),"downstream_blocked_cycles");

        }

    }

}

