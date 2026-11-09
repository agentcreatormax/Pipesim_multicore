#include "pa4.h"
namespace multicore {

    int select_output_port(
        const RoutingContext& context ,
        const SystemConfig& configuration) {

        //Start
        const auto& curr=context.current_router;
        const auto& dest=context.destination_router;
        if(curr.x==dest.x && curr.y==dest.y)return Port::Local;
        if(configuration.routing=="XY"){
            if(dest.x>curr.x)return Port::PositiveX;
            else if(dest.x<curr.x)return Port::NegativeX;
            else if(dest.y>curr.y)return Port::PositiveY;
            else return Port::NegativeY;
        } 
        else{
            if(dest.y>curr.y)return Port::PositiveY;
            else if(dest.y<curr.y)return Port::NegativeY;
            else if(dest.x>curr.x)return Port::PositiveX;
            else return Port::NegativeX;
        }
        //End

    }

    std::optional<unsigned> select_router_input(
        const std::vector<ArbitrationCandidate>&  candidates ,
        unsigned  round_robin_cursor ,
        const SystemConfig&  configuration ) {

        //Start
        if(candidates.empty())return std::nullopt;
        if(configuration.arbitration=="rr"){
            unsigned best=candidates[0].input;
            unsigned best_dist=(candidates[0].input-round_robin_cursor+15)%15;
            for(const auto& x:candidates){
                unsigned dist=(x.input- round_robin_cursor+15)%15;
                if(dist<best_dist){
                    best_dist=dist;
                    best=x.input;
                }
            }
            return best;
        } 
        else{
            const ArbitrationCandidate* best=nullptr;
            unsigned best_dist=16;
            int best_class=3;
            for(const auto& x:candidates){
                int msg=static_cast<int>(x.input%3);
                int p=(msg==2)?0:(msg==1)?1:2;
                unsigned dist=(x.input-round_robin_cursor+15)%15;
                if(p<best_class||(p==best_class && dist<best_dist)){
                    best_class=p;
                    best_dist=dist;
                    best=&x;
                }
            }
            return best?std::optional<unsigned>(best->input):std::nullopt;
        }
        //End
    }

}

