#include "pa4.h"
namespace multicore {

    void count_event(
        MulticoreSystem& system,
        const std::string& component ,
        const std::string&  event ,
        uint64_t  amount) {

        //Start
        std::string key=component+"."+event;
        if(event.size()>=4 && event.compare(event.size()-4,4,"_max")==0){
            system.counters.maximum(key,amount);
        } 
        else system.counters.add(key,amount);
    }

    void sample_occupancy_and_stalls(MulticoreSystem& system  ) {

        //Start
        for(int i=0;i<system.configuration.tiles();i++){
            uint64_t sum=0;
            for(unsigned j=0;j<15;j++){
                uint64_t len=system.current.network.buffers[i][j].size();
                sum+=len;
                std::string s="noc."+std::to_string(i)+".queue."+std::to_string(j);
                system.counters.add(s+".occupancy_sum",len);
                system.counters.maximum(s+".occupancy_max",len);
                system.counters.add(s+".samples",1);
            }
            std::string s="noc."+std::to_string(i);
            system.counters.add(s+".queue_occupancy_sum",sum);
            system.counters.maximum(s+".queue_occupancy_max",sum);
            system.counters.add(s+".samples",1);
        }
        for(int i=0;i<system.configuration.controllers;i++){
            uint64_t len=system.current.controllers[i].queue.size();
            std::string s="mc."+std::to_string(i);
            system.counters.add(s+".queue_occupancy_sum",len);
            system.counters.maximum(s+".queue_occupancy_max",len);
            system.counters.add(s+".samples",1);
        }
        for(int i=0;i<system.configuration.homes;i++){
            uint64_t len=system.current.llc[i].requests.size();
            std::string s="llc."+std::to_string(i);
            system.counters.add(s+".queue_occupancy_sum",len);
            system.counters.maximum(s+".queue_occupancy_max",len);
            system.counters.add(s+".samples",1);
        }
        for(int i=0;i<system.configuration.cores;i++){
            const auto& ord=system.current.order[i];
            uint64_t len=ord.stores.size();
            std::string s="order."+std::to_string(i);
            system.counters.add(s+".queue_occupancy_sum",len);
            system.counters.maximum(s+".queue_occupancy_max",len);
            system.counters.add(s+".samples",1);
            if(system.current.cpu[i].has_value()){
                const auto& req=*system.current.cpu[i];
                bool flag=ord.inflight.has_value()||system.current.cache_input[i].has_value();
                if(req.operation==MemoryOperation::Store && ord.stores.size()>=system.configuration.store_entries){
                    system.counters.add(s+".buffer_full_cycles",1);
                }
                if(req.operation==MemoryOperation::Fence && (!ord.stores.empty() || flag)){
                    system.counters.add(s+".fence_wait_cycles",1);
                }
                if(flag || ((req.operation==MemoryOperation::Fence || req.operation==MemoryOperation::Exchange) && !ord.stores.empty())){
                    system.counters.add(s+".ordering_stall_cycles",1);
                }
            }
        }
        //End
    }

}

