#include "pa4.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <limits>
namespace multicore {

    const char* name(MessageType m) {

        static const char* n[]= {

            "GetS","GetM","PutS","PutM","RecallS","RecallI","Inv","Ack","DataS","DataM","PutAck","GrantAck","MemRead","MemWrite","MemData","MemAck"
        };

        return n[unsigned(m)];

    }

    const char* name(MemoryOperation o) {

        static const char* n[]= {

            "load","store","fence","exchange"
        };

        return n[unsigned(o)];

    }

    int channel(MessageType m) {

        switch(m) {

            case MessageType::GetS:case MessageType::GetM:case MessageType::PutS:case MessageType::PutM:case MessageType::MemRead:case MessageType::MemWrite:return 0;

            case MessageType::RecallS:case MessageType::RecallI:case MessageType::Inv:return 1;

            default:return 2;

        }

    }

    void SystemConfig::validate()const {

        if(x<1||y<1||tiles()>256||cores<1||homes<1||controllers<1||cores+homes+controllers>tiles())throw std::runtime_error("invalid mesh/endpoint counts (one endpoint per tile)");

        if(!router_depth||!injection_depth||!link_bytes||!link_latency||!router_latency||!cache_lines||!llc_lines||!llc_latency||!llc_queue_entries||!store_entries||!mc_entries||!banks||memory_bytes%64||memory_bytes==0)throw std::runtime_error("invalid capacity/timing");

        if(!tRCD||!tRP||!tRAS||!tCL||!tCCD||!tBURST||!tWR||!tREFI||!tRFC||tREFI<=tRFC)throw std::runtime_error("invalid DRAM timing");

        if(routing != "XY" && routing != "YX") throw std::runtime_error("PA4 requires a 2D mesh and XY or YX routing");

        if(memory_model!="SC"&&memory_model!="TSO")throw std::runtime_error("memory_model must be SC or TSO");

        if(arbitration!="rr"&&arbitration!="custom"&&arbitration!="oldest")throw std::runtime_error("arbitration must be rr or custom");

        if(scheduler!="fcfs"&&scheduler!="custom"&&scheduler!="rowhit")throw std::runtime_error("scheduler must be fcfs or custom");

    }

    SystemConfig SystemConfig::load(const std::string& path) {

        SystemConfig c;

        std::ifstream f(path);

        if(!f)throw std::runtime_error("cannot open config");

        std::string l;

        unsigned ln=0;

        while(std::getline(f,l)) {

            ++ln;

            l=l.substr(0,l.find('#'));

            std::istringstream s(l);

            std::string k,v,extra;

            if(!(s>>k))continue;

            if(!(s>>v)||(s>>extra))throw std::runtime_error("config line "+std::to_string(ln));

#define NUM(field) if(k==#field){size_t used=0;auto n=std::stoull(v,&used,0);if(used!=v.size()||n>std::numeric_limits<unsigned>::max())throw std::runtime_error("bad numeric config");c.field=decltype(c.field)(n);continue;}
            NUM(x) NUM(y) NUM(cores) NUM(homes) NUM(controllers) NUM(router_depth) NUM(injection_depth) NUM(link_bytes) NUM(link_latency) NUM(router_latency) NUM(cache_lines) NUM(llc_lines) NUM(llc_latency) NUM(llc_queue_entries) NUM(store_entries) NUM(mc_entries) NUM(banks) NUM(tRCD) NUM(tRP) NUM(tRAS) NUM(tCL) NUM(tCCD) NUM(tBURST) NUM(tWR) NUM(tRTW) NUM(tWTR) NUM(tREFI) NUM(tRFC) NUM(memory_bytes)
#undef NUM
            if(k=="routing")c.routing=v;

            else if(k=="arbitration")c.arbitration=v;

            else if(k=="memory_model")c.memory_model=v;

            else if(k=="scheduler")c.scheduler=v;

            else throw std::runtime_error("unknown config key "+k);

        }

        c.validate();

        return c;

    }

    void Counters::add(const std::string& k,uint64_t n) {

        if(k.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._")!=std::string::npos)throw std::runtime_error("unsafe counter name");

        auto& v=values[k];

        if(n>UINT64_MAX-v)throw std::runtime_error("counter overflow "+k);

        v+=n;

    }

    void Counters::maximum(const std::string& k,uint64_t n) {

        values[k]=std::max(values[k],n);

    }

    uint64_t Counters::get(const std::string& k)const {

        auto i=values.find(k);

        return i==values.end()?0:i->second;

    }

    void Counters::json(const std::string& p)const {

        std::ofstream f(p);

        if(!f)throw std::runtime_error("cannot write "+p);

        f<<"{\n";

        bool first=true;

        for(auto& [k,v]:values) {

            if(!first)f<<",\n";

            f<<"  \""<<k<<"\": "<<v;

            first=false;

        }

        f<<"\n}\n";

    }

    MulticoreSystem::MulticoreSystem(SystemConfig c):configuration(std::move(c)) {

        configuration.validate();

        reset();

    }

    void MulticoreSystem::reset() {

        cycle=0;

        begun=false;

        packet_next=1;

        memory_next=uint64_t(1)<<48;

        current= {

        };

        next= {

        };

        counters= {

        };

        observer= {

        };

        trace.clear();

        memory.assign(configuration.memory_bytes/4,0);

        auto& n=current.network;

        n.buffers.resize(configuration.tiles());

        n.reserved.resize(configuration.tiles());

        n.link_ready.resize(configuration.tiles());

        n.round_robin_cursor.resize(configuration.tiles());

        n.inject.resize(configuration.tiles());

        n.inbox.resize(configuration.tiles());

        current.caches.resize(configuration.cores);

        for(auto& c:current.caches)c.lines.resize(configuration.cache_lines);

        current.order.resize(configuration.cores);

        current.cpu.resize(configuration.cores);

        current.cache_input.resize(configuration.cores);

        current.results.resize(configuration.cores);

        current.cache_results.resize(configuration.cores);

        current.llc.resize(configuration.homes);

        for(auto& bank:current.llc) bank.entries.resize(configuration.llc_lines);

        current.controllers.resize(configuration.controllers);

        for(auto& m:current.controllers) {

            m.banks.resize(configuration.banks);

            m.refresh_due=configuration.tREFI;

        }

        // Register required counter names with zero values.
        const std::map<std::string,std::vector<std::string>> names= {

            {

                "noc", {

                    "packets_injected","packets_delivered","link_hops","link_bytes","grants","arbitration_losses","downstream_blocked_cycles","queue_occupancy_sum","queue_occupancy_max","samples","delivered_latency_sum","delivered_latency_max"
                }

            },  {

                "cache", {

                    "loads_accepted","stores_accepted","atomics_accepted","atomic_permission_hits","atomic_misses","load_permission_hits","store_permission_hits","read_misses","write_misses","upgrades","invalidations_received","invalidations_applied","acks_sent","dirty_evictions","writebacks_sent","accesses_completed"
                }

            },  {

                "directory", {

                    "requests_accepted","transactions_started","transactions_completed","recalls_sent","invalidations_sent","acks_received","ownership_grants","conflicts_queued","completed_latency_sum"
                }

            },  {

                "order", {

                    "stores_enqueued","stores_drained","loads_forwarded","loads_bypassed","ordering_stall_cycles","buffer_full_cycles","fences_started","fences_completed","fence_wait_cycles","queue_occupancy_sum","queue_occupancy_max","samples"
                }

            },  {
                "llc", {
                    "requests_accepted","requests_completed","hits","misses","dirty_evictions",
                    "queue_occupancy_sum","queue_occupancy_max","samples"
                }
            },  {

                "mc", {

                    "requests_accepted","requests_completed","requests_service_started","row_hits","row_misses","row_conflicts","read_bytes","write_bytes","queue_occupancy_sum","queue_occupancy_max","samples","completed_latency_sum"
                }

            }

        };

        for(auto& [k,ns]:names) {

            int count=k=="noc"?configuration.tiles():(k=="directory"||k=="llc")?configuration.homes:k=="mc"?configuration.controllers:configuration.cores;

            for(int i=0;i<count;++i)for(auto& n:ns)counters.values[k+"."+std::to_string(i)+"."+n]=0;

        }

        next=current;

    }

    MeshCoordinate MulticoreSystem::coord(int tile_id) const {

        if (tile_id < 0 || tile_id >= configuration.tiles()) throw std::runtime_error("Tile out of range");

        return {

            tile_id % configuration.x, tile_id / configuration.x
        };

    }

    int MulticoreSystem::tile(MeshCoordinate coordinate) const {

        if (coordinate.x < 0 || coordinate.y < 0 ||         coordinate.x >= configuration.x || coordinate.y >= configuration.y) return -1;

        return coordinate.y * configuration.x + coordinate.x;

    }

    int MulticoreSystem::neighbor(int tile_id, int port) const {

        auto coordinate = coord(tile_id);

        switch (port) {

            case PositiveX: ++coordinate.x;

            break;

            case NegativeX: --coordinate.x;

            break;

            case PositiveY: ++coordinate.y;

            break;

            case NegativeY: --coordinate.y;

            break;

            case Local: return tile_id;

            default: throw std::runtime_error("Invalid router port");

        }

        return tile(coordinate);

    }

    int MulticoreSystem::home(uint32_t a)const {

        return configuration.cores+int((a/64)%configuration.homes);

    }

    int MulticoreSystem::controller(uint32_t a)const {

        return configuration.cores+configuration.homes+int((a/64)%configuration.controllers);

    }

    unsigned MulticoreSystem::bank(uint32_t a)const {

        return ((a/64)/configuration.controllers)%configuration.banks;

    }

    unsigned MulticoreSystem::row(uint32_t a)const {

        return ((a/64)/configuration.controllers)/configuration.banks/16;

    }

    void MulticoreSystem::initialize(uint32_t a,uint32_t v) {

        if(cycle||begun||a%4||a>=configuration.memory_bytes)throw std::runtime_error("invalid initialization");

        memory[a/4]=v;

    }

    CacheLineData MulticoreSystem::initial_line(uint32_t a)const {

        if(a%64||a+64>configuration.memory_bytes)throw std::runtime_error("line out of range");

        CacheLineData d;

        for(unsigned w=0;w<WordsPerCacheLine;++w)d[w]=memory[a/4+w];

        return d;

    }

    uint32_t MulticoreSystem::architectural_word(uint32_t a)const {

        if(a%4||a>=configuration.memory_bytes)throw std::runtime_error("word out of range");

        for(auto& c:current.caches) {

            auto& l=c.lines[slot(a)];

            if(l.tag==line_address(a)&&l.state==CacheState::M)return l.data[(a%64)/4];

        }

        for(const auto& bank:current.llc) for(const auto& entry:bank.entries) if(entry.valid&&entry.tag==line_address(a)) return entry.data[(a%64)/4];

        return memory[a/4];

    }

    void MulticoreSystem::begin() {

        if(begun)throw std::runtime_error("double begin");

        next=current;

        begun=true;

    }

    bool MulticoreSystem::submit(const MemoryRequest& r) {

        if(!begun)throw std::runtime_error("submit outside cycle");

        if(r.core<0||r.core>=configuration.cores||r.address%4||r.address>=configuration.memory_bytes)throw std::runtime_error("bad CPU request");

        if(current.cpu[r.core]||next.cpu[r.core])return false;

        auto q=r;

        q.accepted=cycle;

        next.cpu[r.core]=q;

        observer.add("cpu.accepted");

        if(r.operation==MemoryOperation::Fence)event("order."+std::to_string(r.core),"fences_started");

        return true;

    }

    std::optional<MemoryResult> MulticoreSystem::result(int c) {

        if(!begun||c<0||c>=configuration.cores)throw std::runtime_error("bad result poll");

        if(current.results[c].empty()||next.results[c].empty())return {

        };

        auto r=current.results[c].front();

        next.results[c].pop_front();

        return r;

    }

    Packet MulticoreSystem::message(MessageType m,int s,int d,uint32_t a,uint64_t transaction_id,const CacheLineData& data,int who)const {

        Packet p;

        p.type=m;

        p.source=s;

        p.destination=d;

        p.line_address=a;

        p.transaction_id=transaction_id;

        p.data=data;

        p.requester=who;

        return p;

    }

    bool MulticoreSystem::can_send(int source_tile, MessageType type, unsigned count)const {
        if (!begun) throw std::runtime_error("can_send outside cycle");
        coord(source_tile);
        const auto& queue = next.network.inject[source_tile];
        const auto occupied = std::count_if(queue.begin(), queue.end(), [&](const Packet& packet) {
            return channel(packet.type) == channel(type);
        });
        const bool home_snoop = source_tile >= configuration.cores &&
            source_tile < configuration.cores + configuration.homes && channel(type) == 1;
        if (home_snoop && (count > 1 || std::any_of(queue.begin(), queue.end(), [&](const Packet& packet) {
            return channel(packet.type) == 1 && packet.injected == cycle;
        }))) return false;
        return count <= configuration.injection_depth &&
            static_cast<unsigned>(occupied) <= configuration.injection_depth - count;
    }

    bool MulticoreSystem::send(Packet p) {

        if(!begun)throw std::runtime_error("send outside cycle");

        coord(p.source);

        coord(p.destination);

        if(p.line_address%64||p.line_address+64>configuration.memory_bytes)throw std::runtime_error("bad packet line");

        if (!can_send(p.source, p.type)) return false;

        p.id=packet_next++;

        p.injected=cycle;

        next.network.inject[p.source].push_back(p);

        observer.add("noc.injected");

        event("noc."+std::to_string(p.source),"packets_injected");

        event("noc."+std::to_string(p.source),"injected_"+std::string(name(p.type)));

        observer.add(std::string("noc.injected.")+name(p.type));

        log("inject "+std::to_string(p.id)+" "+name(p.type)+" "+std::to_string(p.source)+" "+std::to_string(p.destination));

        return true;

    }

    void MulticoreSystem::event(const std::string& component,const std::string& event,uint64_t amount) {

        multicore::count_event(*this,component,event,amount);

    }

    void MulticoreSystem::log(const std::string& e) {

        trace.push_back(std::to_string(cycle)+" "+e);

    }

    void MulticoreSystem::cache_complete(int c,uint32_t v) {

        if(!next.caches[c].pending)throw std::runtime_error("cache completion without pending owner");

        auto r=*next.caches[c].pending;

        next.cache_results[c].push_back( {

            r.id,v,cycle+1
        });

        next.caches[c].pending.reset();

        next.caches[c].phase="idle";

        observer.add("cache.completed");

        event("cache."+std::to_string(c),"accesses_completed");

        log("memory_event "+std::to_string(c)+" "+std::to_string(r.id)+" "+name(r.operation)+" "+std::to_string(r.address)+" "+std::to_string(r.value)+" "+std::to_string(v));

        log("cache_complete "+std::to_string(c)+" "+std::to_string(r.id)+" "+std::to_string(v));

    }

    void MulticoreSystem::cpu_complete(int c,const MemoryRequest& r,uint32_t v) {

        next.results[c].push_back( {

            r.id,v,cycle+1
        });

        observer.add("cpu.completed");

        log("cpu_complete "+std::to_string(c)+" "+std::to_string(r.id)+" "+std::to_string(v));

    }

    void MulticoreSystem::evaluate_network_only() {

        if(!begun)throw std::runtime_error("network evaluation outside cycle");

        multicore::sample_occupancy_and_stalls(*this);

        network();

    }

    void MulticoreSystem::evaluate() {

        if(!begun)throw std::runtime_error("evaluate outside cycle");

        multicore::sample_occupancy_and_stalls(*this);

        network();

        ordering();

        endpoints();

        llc();

        dram();

    }

    void MulticoreSystem::commit() {

        if(!begun)throw std::runtime_error("commit outside cycle");

        current=std::move(next);

        begun=false;

        ++cycle;

        check();

    }

    void MulticoreSystem::tick() {

        begin();

        evaluate();

        commit();

    }

    bool MulticoreSystem::idle()const {

        for(auto& x:current.cpu)if(x)return false;

        for(auto& x:current.cache_input)if(x)return false;

        for(auto& c:current.caches)if(c.pending)return false;

        for(auto& o:current.order)if(o.inflight||!o.stores.empty())return false;

        for(auto& [a,d]:current.directory) {

            (void)a;

            if(d.active||!d.waiting.empty())return false;

        }

        for(auto& v:current.network.inject)if(!v.empty())return false;

        for(auto& v:current.network.inbox)if(!v.empty())return false;

        for(auto& b:current.network.buffers)for(auto& q:b)if(!q.empty())return false;

        if(!current.network.flights.empty())return false;

        for(auto& b:current.llc)if(b.active||!b.requests.empty()||!b.replies.empty())return false;

        for(auto& m:current.controllers) {

            if(!m.queue.empty())return false;

            for(auto& b:m.banks)if(b.work)return false;

        }

        return true;

    }

    void MulticoreSystem::check()const {

        uint64_t packets=current.network.flights.size();

        for(auto& q:current.network.inject)packets+=q.size();

        for(auto& b:current.network.buffers)for(auto& q:b)packets+=q.size();

        if(observer.get("noc.injected")!=observer.get("noc.delivered")+packets)throw std::runtime_error("packet conservation failed");

        for(auto& c:current.caches)for(auto& l:c.lines)if(l.state==CacheState::M)for(auto& d:current.caches)if(&c!=&d)for(auto& other:d.lines)if(l.tag==other.tag&&other.state!=CacheState::I)throw std::runtime_error("MSI permission invariant failed");

    }

    void MulticoreSystem::save(const std::string& base)const {

        counters.json(base+".counters.json");

        observer.json(base+".observer.json");

        std::ofstream t(base+".trace");

        for(auto& e:trace)t<<e<<'\n';

        std::ofstream s(base+".state.json");

        s<<"{\"cycles\":"<<cycle<<",\"memory\":{";

        bool first=true;

        for(unsigned i=0;i<memory.size();++i) {

            auto v=architectural_word(i*4);

            if(!v)continue;

            if(!first)s<<',';

            first=false;

            s<<'\"'<<i*4<<"\":"<<v;

        }

        s<<"}}\n";

    }

}

