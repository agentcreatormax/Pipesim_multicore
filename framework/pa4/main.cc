#include "pa4.h"
#include "pipesim.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
using namespace multicore;

struct Job {

    Cycle at;

    MemoryRequest request;

};

static std::vector<Job> read_jobs(const std::string& path) {

    std::ifstream f(path);

    if(!f)throw std::runtime_error("cannot open workload");

    std::vector<Job> jobs;

    std::string line;

    uint64_t id=1;

    unsigned lineno=0;

    while(std::getline(f,line)) {

        ++lineno;

        line=line.substr(0,line.find('#'));

        std::istringstream s(line);

        std::string operation,extra;

        Job j;

        if(!(s>>j.at))continue;

        if(!(s>>j.request.core>>operation))throw std::runtime_error("bad workload line");

        j.request.id=id++;

        if(operation=="LOAD")j.request.operation=MemoryOperation::Load;

        else if(operation=="STORE")j.request.operation=MemoryOperation::Store;

        else if(operation=="FENCE")j.request.operation=MemoryOperation::Fence;

        else if(operation=="XCHG")j.request.operation=MemoryOperation::Exchange;

        else throw std::runtime_error("bad opcode");

        if(j.request.operation!=MemoryOperation::Fence) {

            std::string a;

            if(!(s>>a))throw std::runtime_error("missing address");

            j.request.address=std::stoul(a,nullptr,0);

        }

        if(j.request.operation==MemoryOperation::Store||j.request.operation==MemoryOperation::Exchange) {

            std::string v;

            if(!(s>>v))throw std::runtime_error("missing value");

            j.request.value=std::stoul(v,nullptr,0);

        }

        if(s>>extra)throw std::runtime_error("extra workload field at line "+std::to_string(lineno));

        jobs.push_back(j);

    }

    return jobs;

}

int main(int argc,char** argv) {

    std::unique_ptr<MulticoreSystem> ptr;

    std::string base="run";

    try {

        if(argc<5) {

            std::cerr<<"Usage: pa4 trace CONFIG WORKLOAD OUTPUT [MAX_CYCLES]\n       pa4 cpu CONFIG PA3_CONFIG PROGRAM_LIST OUTPUT [MAX_CYCLES]\n";

            return 2;

        }

        bool cpu=std::string(argv[1])=="cpu";

        if(cpu&&argc<6)throw std::runtime_error("cpu mode needs program list and output");

        if(!cpu&&std::string(argv[1])!="trace")throw std::runtime_error("mode must be trace or cpu");

        base=argv[cpu?5:4];

        auto c=SystemConfig::load(argv[2]);

        ptr=std::make_unique<MulticoreSystem>(c);

        auto& sys=*ptr;

        Cycle limit=argc>(cpu?6:5)?std::stoull(argv[cpu?6:5]):100000;

        std::vector<std::unique_ptr<pipesim::Machine>> machines;

        std::vector<std::unique_ptr<pipesim::Processor>> cores;

        std::vector<Job> jobs;

        std::vector<bool> issued;

        std::vector<std::optional<uint64_t>> waiting(c.cores);

        std::ofstream results(base+".results.csv");

        if(!results)throw std::runtime_error("output directory does not exist");

        results<<"cycle,core,id,value\n";

        if(cpu) {

            std::ifstream list(argv[4]);

            if(!list)throw std::runtime_error("cannot open program list");

            std::string path;

            std::map<uint32_t,uint32_t> initial;

            while(list>>path) {

                auto m=std::make_unique<pipesim::Machine>();

                std::string err;

                if(!m->LoadConfiguration(argv[3],&err)||!m->LoadProgram(path,&err))throw std::runtime_error(path+": "+err);

                if(m->Configuration().data_memory.image_bytes!=c.memory_bytes)throw std::runtime_error("PA3/PA4 memory sizes differ");

                std::ifstream program(path);

                std::string ln;

                while(std::getline(program,ln)) {

                    std::istringstream in(ln);

                    std::string word,a,v;

                    if(in>>word&&word=="DATA") {

                        in>>a>>v;

                        auto address=std::stoul(a,nullptr,0),value=std::stoul(v,nullptr,0);

                        if(initial.count(address)&&initial[address]!=value)throw std::runtime_error("conflicting DATA images");

                        initial[address]=value;

                    }

                }

                int id=machines.size();

                if(id>=c.cores)throw std::runtime_error("too many core programs");

                auto processor=pipesim::BuildDesign(m.get());

                processor->Reset();

                m->AttachDataPort([&,id](uint64_t seq,pipesim::Opcode operation,uint32_t a,uint32_t v) {

                    MemoryOperation kind=operation==pipesim::Opcode::kSw?MemoryOperation::Store:operation==pipesim::Opcode::kFence?MemoryOperation::Fence:operation==pipesim::Opcode::kXchg?MemoryOperation::Exchange:MemoryOperation::Load;  return sys.submit( {

                        seq,id,kind,a,v,0
                    });
                },[&,id]()->std::optional<std::pair<uint64_t,uint32_t>> {

                    auto r=sys.result(id);  if(!r)return {

                    };  return std::make_pair(r->id,r->value);
                });

                cores.push_back(std::move(processor));

                machines.push_back(std::move(m));

            }

            if(int(cores.size())!=c.cores)throw std::runtime_error("program list must have one path per core");

            for(auto& [a,v]:initial)sys.initialize(a,v);

        }

        else {

            jobs=read_jobs(argv[3]);

            issued.resize(jobs.size());

        }

        bool done=false;

        for(Cycle t=0;t<limit;++t) {

            sys.begin();

            if(cpu) {

                for(auto& core:cores)core->Clock();

            }

            else {

                for(int id=0;id<c.cores;++id) {

                    if(waiting[id]) {

                        auto r=sys.result(id);

                        if(r) {

                            if(r->id!=*waiting[id])throw std::runtime_error("trace result owner mismatch");

                            results<<sys.cycle<<','<<id<<','<<r->id<<','<<r->value<<'\n';

                            waiting[id].reset();

                        }

                    }

                    if(waiting[id])continue;

                    for(unsigned j=0;j<jobs.size();++j)if(!issued[j]&&jobs[j].request.core==id) {

                        if(jobs[j].at<=t&&sys.submit(jobs[j].request)) {

                            issued[j]=true;

                            waiting[id]=jobs[j].request.id;

                        }

                        break;

                    }

                }

            }

            sys.evaluate();

            sys.commit();

            done=sys.idle();

            if(cpu) {

                for(auto& core:cores)done=done&&core->IsHalted();

            }

            else {

                for(auto b:issued)done=done&&b;

                for(auto& w:waiting)done=done&&!w;

            }

            if(done)break;

        }

        sys.save(base);

        if(cpu) {

            std::ofstream f(base+".registers.csv");

            f<<"core,register,value\n";

            for(int i=0;i<c.cores;++i)for(int r=0;r<8;++r)f<<i<<','<<r<<','<<machines[i]->ReadRegister(r)<<'\n';

        }

        if(!done)throw std::runtime_error("cycle limit exceeded; inspect saved trace");

        std::cout<<"Completed in "<<sys.cycle<<" cycles\n";

        return 0;

    }

    catch(const std::exception& e) {

        if(ptr)ptr->save(base);

        std::cerr<<e.what()<<'\n';

        return 1;

    }

}

