#include "pa4.h"
namespace multicore {

    MemoryOrderDecision decide_memory_action(
        const MemoryRequest&  request ,
        const MemoryOrderUnit& order_unit ,
        const SystemConfig&  configuration) {

        //Start
        switch(request.operation){
            case MemoryOperation::Fence:{
                if(!order_unit.stores.empty())return {MemoryAction::Wait,0};
                return {MemoryAction::CompleteFence,0};
            }
            case MemoryOperation::Exchange:{
                if(!order_unit.stores.empty())return {MemoryAction::Wait,0};
                return {MemoryAction::Issue,0};
            }
            case MemoryOperation::Store:{
                if(order_unit.stores.size()<configuration.store_entries){
                    return {MemoryAction::BufferStore,0};
                }
                return {MemoryAction::Wait,0};
            }
            case MemoryOperation::Load:{
                for(auto it=order_unit.stores.rbegin();it!=order_unit.stores.rend();it++){
                    if(it->address==request.address)return {MemoryAction::Forward,it->value};
                }
                return {MemoryAction::Issue,0};
            }
        }
        return {MemoryAction::Wait,0};
        //End

    }

}

