#include "pa4.h"
#include <algorithm>
namespace multicore {

    void MulticoreSystem::dram() {

        for(int controller_index=0;controller_index<configuration.controllers;++controller_index) {

            auto& current_controller=current.controllers[controller_index];

            auto& next_controller=next.controllers[controller_index];

            int endpoint=configuration.cores+configuration.homes+controller_index;

            // Completed bursts wait for response injection capacity; data applied once at completion.
            for(unsigned bank_index=0;bank_index<configuration.banks;++bank_index) {

                auto& current_bank=current_controller.banks[bank_index];

                auto& next_bank=next_controller.banks[bank_index];

                if(current_bank.work&&current_bank.phase=="burst"&&cycle>=current_bank.finish) {

                    auto packet=*current_bank.work;

                    auto data=initial_line(packet.line_address);

                    if(packet.type==MessageType::MemWrite)data=packet.data;

                    auto response=message(packet.type==MessageType::MemWrite?MessageType::MemAck:MessageType::MemData,endpoint,packet.source,packet.line_address,packet.transaction_id,data,packet.requester);

                    if(send(response)) {

                        if(packet.type==MessageType::MemWrite)for(unsigned w=0;w<WordsPerCacheLine;++w)memory[packet.line_address/4+w]=packet.data[w];

                        next_bank.work.reset();

                        next_bank.phase="idle";

                        observer.add("mc.completed");

                        event("mc."+std::to_string(controller_index),"requests_completed");

                        event("mc."+std::to_string(controller_index),"completed_latency_sum",cycle+1-packet.controller_arrival);

                        event("mc."+std::to_string(controller_index),packet.type==MessageType::MemWrite?"write_bytes":"read_bytes",64);

                        observer.add("mc.latency_sum",cycle+1-packet.controller_arrival);

                        log("dram_complete "+std::to_string(controller_index)+" "+std::to_string(packet.transaction_id));

                        /* PA4-COUNT: completions, latency, read/write bytes */

                    }

                }

            }

            if(cycle>=current_controller.refresh_due) {

                bool safe=cycle>=current_controller.bus_ready;

                for(auto& current_bank:current_controller.banks)if(current_bank.work||cycle<current_bank.ready||(current_bank.open_row>=0&&cycle<current_bank.activated+configuration.tRAS))safe=false;

                if(safe) {

                    next_controller.refresh_until=cycle+configuration.tRP+configuration.tRFC;

                    next_controller.refresh_due=cycle+configuration.tRP+configuration.tRFC+configuration.tREFI;

                    for(auto& current_bank:next_controller.banks) {

                        current_bank.open_row=-1;

                        current_bank.ready=next_controller.refresh_until;

                    }

                    observer.add("dram.REF");

                    log("command REF "+std::to_string(controller_index));

                    continue;

                }

                // Finish in-service requests while refresh waits; admit no new work.
            }

            if(cycle<current_controller.refresh_until)continue;

            std::vector<unsigned> eligible_requests;

            for(unsigned request_index=0;request_index<current_controller.queue.size();++request_index) {

                auto current_bank=bank(current_controller.queue[request_index].line_address);

                if(!current_controller.banks[current_bank].work)eligible_requests.push_back(request_index);

            }

            if(!eligible_requests.empty() && cycle < current_controller.refresh_due) {

                auto selected_request=multicore::select_memory_request(eligible_requests,current_controller,cycle,configuration);

                if(selected_request) {

                    if(std::find(eligible_requests.begin(),eligible_requests.end(),*selected_request)==eligible_requests.end())throw std::runtime_error("scheduler selected illegal request");

                    auto packet=current_controller.queue[*selected_request];

                    auto current_bank=bank(packet.line_address);

                    if(next_controller.banks[current_bank].work)throw std::runtime_error("double bank assignment");

                    next_controller.banks[current_bank].work=packet;

                    next_controller.banks[current_bank].phase="prepare";

                    next_controller.queue.erase(next_controller.queue.begin()+*selected_request);

                    observer.add("mc.service_started");

                    event("mc."+std::to_string(controller_index),"requests_service_started");
                    if (packet.requester >= 0 && packet.requester < configuration.cores) {
                        const std::string requester = "mc." + std::to_string(controller_index) + ".core." + std::to_string(packet.requester);
                        event(requester, "queue_wait_cycles_sum", cycle - packet.controller_arrival);
                        event(requester, "queue_wait_cycles_max", cycle - packet.controller_arrival);
                    }

                    std::string kind=current_controller.banks[current_bank].open_row<0?"row_misses":unsigned(current_controller.banks[current_bank].open_row)==row(packet.line_address)?"row_hits":"row_conflicts";

                    observer.add("mc."+kind);

                    event("mc."+std::to_string(controller_index),kind);

                    event("mc."+std::to_string(controller_index)+".bank."+std::to_string(current_bank),kind);

                    log("dram_start "+std::to_string(controller_index)+" "+std::to_string(packet.transaction_id)+" "+kind);

                    /* PA4-COUNT: service classification */

                }

            }

            // One command per controller per cycle. No newly assigned request can issue yet.
            for(unsigned bank_index=0;bank_index<configuration.banks;++bank_index) {

                auto& current_bank=current_controller.banks[bank_index];

                auto& next_bank=next_controller.banks[bank_index];

                if(!current_bank.work||current_bank.phase=="burst"||cycle<current_bank.ready)continue;

                auto packet=*current_bank.work;

                unsigned target=row(packet.line_address);

                std::string command;

                if(current_bank.open_row>=0&&unsigned(current_bank.open_row)!=target) {

                    if(cycle<current_bank.activated+configuration.tRAS)continue;

                    command="PRE";

                    next_bank.open_row=-1;

                    next_bank.ready=cycle+configuration.tRP;

                }

                else if(current_bank.open_row<0) {

                    command="ACT";

                    next_bank.open_row=int(target);

                    next_bank.activated=cycle;

                    next_bank.ready=cycle+configuration.tRCD;

                }

                else {

                    bool write=packet.type==MessageType::MemWrite;

                    Cycle turnaround=current_controller.last_write&&!write?configuration.tWTR:!current_controller.last_write&&write?configuration.tRTW:0;

                    if(cycle<current_controller.column_ready||cycle<current_controller.bus_ready+turnaround)continue;

                    command=write?"WR":"RD";

                    next_bank.phase="burst";

                    next_bank.finish=cycle+configuration.tCL+configuration.tBURST;

                    next_bank.ready=next_bank.finish+(write?configuration.tWR:0);

                    next_controller.bus_ready=next_bank.finish;

                    next_controller.column_ready=cycle+configuration.tCCD;

                    next_controller.last_write=write;

                }

                observer.add("dram."+command);

                event("mc."+std::to_string(controller_index),"command_"+command);

                log("command "+command+" "+std::to_string(controller_index)+" "+std::to_string(bank_index)+" "+std::to_string(target));

                break;

            }

        }

    }

}

