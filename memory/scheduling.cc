#include "pa4.h"
namespace multicore {

    std::optional<unsigned> select_memory_request(
        const std::vector<unsigned>& eligible_requests ,
        const MemoryController&  controller ,
        Cycle cycle ,
        const SystemConfig&  configuration ) {

        //Start
        if(eligible_requests.empty())return std::nullopt;
        if(configuration.scheduler=="fcfs"){
            unsigned best_idx=eligible_requests[0];
            Cycle best_arrival=controller.queue[best_idx].controller_arrival;
            for(unsigned x:eligible_requests){
                Cycle arr=controller.queue[x].controller_arrival;
                if(arr<best_arrival || (arr==best_arrival && x<best_idx)){
                    best_arrival=arr;
                    best_idx=x;
                }
            }
            return best_idx;
        } 
        else{
            constexpr Cycle limit=50;
            auto hit=[&] (unsigned x) ->bool {
                const auto& p=controller.queue[x];
                uint32_t idx=p.line_address/64;
                unsigned b=(idx/configuration.controllers)%configuration.banks;
                unsigned r=(idx/configuration.controllers)/configuration.banks/16;
                int open=controller.banks[b].open_row;
                return open>=0 && static_cast<unsigned>(open)==r;
            };
            auto tier=[&](unsigned x)->int {
                const auto& p=controller.queue[x];
                if(cycle>=p.controller_arrival+limit)return 0;
                if(hit(x))return 1;
                return 2;
            };
            unsigned best_idx=eligible_requests[0];
            int best_tier=tier(best_idx);
            Cycle best_arrival=controller.queue[best_idx].controller_arrival;
            for(unsigned x:eligible_requests){
                int t=tier(x);
                Cycle arr=controller.queue[x].controller_arrival;
                if(t<best_tier||(t==best_tier && (arr<best_arrival||(arr==best_arrival&&x<best_idx)))){
                    best_tier=t;
                    best_arrival=arr;
                    best_idx=x;
                }
            }
            return best_idx;
        }
        //End

    }

}

