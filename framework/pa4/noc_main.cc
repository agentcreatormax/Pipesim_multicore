#include "pa4.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
int main(int argc,char** argv) {

    using namespace multicore;

    std::unique_ptr<MulticoreSystem> ptr;

    std::string base="noc";

    try {

        if(argc<4) {

            std::cerr<<"Usage: pa4-noc CONFIG TRAFFIC OUTPUT [MAX_CYCLES]\n";

            return 2;

        }

        base=argv[3];

        ptr=std::make_unique<MulticoreSystem>(SystemConfig::load(argv[1]));

        auto& s=*ptr;

        struct Entry {

            Cycle at;

            Packet p;

            bool sent=false;

        };

        std::vector<Entry> entries;

        std::ifstream f(argv[2]);

        if(!f)throw std::runtime_error("cannot open traffic");

        std::string line;

        while(std::getline(f,line)) {

            line=line.substr(0,line.find('#'));

            std::istringstream in(line);

            Entry e;

            std::string kind,addr,extra;

            if(!(in>>e.at))continue;

            if(!(in>>e.p.source>>e.p.destination>>kind>>addr)||(in>>extra))throw std::runtime_error("traffic: cycle src dst type line");

            bool found=false;

            for(unsigned n=0;n<=unsigned(MessageType::MemAck);++n)if(kind==name(MessageType(n))) {

                e.p.type=MessageType(n);

                found=true;

            }

            if(!found)throw std::runtime_error("unknown packet type");

            s.coord(e.p.source);

            s.coord(e.p.destination);

            e.p.line_address=std::stoul(addr,nullptr,0);

            e.p.transaction_id=entries.size()+1;

            entries.push_back(e);

        }

        Cycle limit=argc>4?std::stoull(argv[4]):100000;

        bool done=false;

        for(Cycle t=0;t<limit;++t) {

            s.begin();

            for(int tile=0;tile<s.configuration.tiles();++tile)if(!s.current.network.inbox[tile].empty())s.next.network.inbox[tile].pop_front();

            for(auto& e:entries)if(!e.sent&&e.at<=t) {

                if(s.send(e.p))e.sent=true;

            }

            s.evaluate_network_only();

            s.commit();

            done=s.idle();

            for(auto& e:entries)done=done&&e.sent;

            if(done)break;

        }

        s.save(base);

        if(!done)throw std::runtime_error("cycle limit exceeded");

        std::cout<<"Delivered "<<s.observer.get("noc.delivered")<<" packets in "<<s.cycle<<" cycles\n";

        return 0;

    }

    catch(const std::exception& e) {

        if(ptr)ptr->save(base);

        std::cerr<<e.what()<<'\n';

        return 1;

    }

}

