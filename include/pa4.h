#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <stdexcept>
namespace multicore {

    using Cycle=uint64_t;

    constexpr unsigned WordsPerCacheLine=16;

    // 64-byte lines; aligned 32-bit accesses
    using CacheLineData=std::array<uint32_t,WordsPerCacheLine>;

    enum class MemoryOperation {

        Load,Store,Fence,Exchange
    };

    enum class MessageType {

        GetS,GetM,PutS,PutM,RecallS,RecallI,Inv,Ack,DataS,DataM,PutAck,GrantAck,MemRead,MemWrite,MemData,MemAck
    };

    enum class CacheState {

        I,S,M
    };

    enum Port {

        PositiveX,NegativeX,PositiveY,NegativeY,Local
    };

    const char* name(MessageType);

    const char* name(MemoryOperation);

    int channel(MessageType);

    struct MeshCoordinate {

        int x=0,y=0;

    };

    struct MemoryRequest {

        uint64_t id=0;

        int core=0;

        MemoryOperation operation=MemoryOperation::Load;

        uint32_t address=0,value=0;

        Cycle accepted=0;

    };

    struct MemoryResult {

        uint64_t id=0;

        uint32_t value=0;

        Cycle completed=0;

    };

    struct Packet {

        // id is assigned by send(); transaction_id is preserved across replies.
        uint64_t id=0,transaction_id=0;

        // source/destination are tile IDs; requester is the original requesting core.
        int source=0,destination=0,requester=0;

        MessageType type=MessageType::GetS;

        // Aligned byte address, not a line index.
        uint32_t line_address=0;

        CacheLineData data {

        };

        Cycle injected=0,ready=0;

        // Memory-controller queue admission; distinct from NoC injection.
        Cycle controller_arrival=0;

    };

    struct SystemConfig {

        int x=3,y=3,cores=4,homes=4,controllers=1;

        unsigned router_depth=4,injection_depth=8,link_bytes=16,link_latency=1,router_latency=1,cache_lines=8,llc_lines=4,llc_latency=3,llc_queue_entries=4,store_entries=4,mc_entries=16,banks=4;

        unsigned tRCD=4,tRP=4,tRAS=8,tCL=4,tCCD=2,tBURST=4,tWR=4,tRTW=3,tWTR=3,tREFI=200,tRFC=12;

        uint32_t memory_bytes=65536;

        std::string routing="XY",arbitration="rr",memory_model="SC",scheduler="fcfs";

        int tiles()const {

            return x*y;

        }

        void validate()const;

        static SystemConfig load(const std::string&);

    };

    struct Counters {

        std::map<std::string,uint64_t> values;

        void add(const std::string&,uint64_t=1);

        void maximum(const std::string&,uint64_t);

        uint64_t get(const std::string&)const;

        void json(const std::string&)const;

    };

    struct CacheLine {

        CacheState state=CacheState::I;

        uint32_t tag=0;

        CacheLineData data {

        };

        bool transient=false;

    };

    struct PrivateCache {

        std::vector<CacheLine> lines;

        std::optional<MemoryRequest> pending;

        // waiting transaction survives dirty eviction; controller selects phase/state.
        std::string phase="idle";

        uint64_t transaction_id=0;

        std::optional<uint32_t> evicted_line_address;

    };

    struct DirectoryEntry {

        CacheState state=CacheState::I;

        int owner=-1;

        std::set<int> sharers;

        bool active=false;

        Packet request;

        std::set<int> pending_acknowledgements;
        // Sharers whose Inv has not yet been injected (one home snoop/cycle).
        std::set<int> unsent_invalidations;
        std::string phase="idle";

        CacheLineData data {

        };

        Cycle started=0;

        // Each line has a bounded conflict queue (router_depth entries).
        std::deque<Packet> waiting;

    };

    struct MemoryOrderUnit {

        std::deque<MemoryRequest> stores;

        std::optional<MemoryRequest> inflight;

        bool draining=false;

    };

    struct DRAMBank {

        int open_row=-1;

        Cycle ready=0,activated=0;

        std::optional<Packet> work;

        std::string phase="idle";

        Cycle finish=0;

    };

    struct MemoryController {

        std::deque<Packet> queue;

        std::vector<DRAMBank> banks;

        Cycle column_ready=0,bus_ready=0,refresh_until=0,refresh_due=0;

        bool last_write=false;

    };

    struct PacketTransfer {

        Packet packet;

        int destination=0,input=0;

        Cycle ready=0;

    };

    struct MeshNetwork {

        // [tile][port*3+protocol_channel]
        std::vector<std::array<std::deque<Packet>,15>> buffers;

        std::vector<std::array<unsigned,15>> reserved;

        std::vector<std::array<Cycle,4>> link_ready;

        std::vector<std::array<unsigned,5>> round_robin_cursor;

        std::vector<std::deque<Packet>> inject, inbox;

        std::vector<PacketTransfer> flights;

    };

    struct LLCEntry {

        bool valid=false,dirty=false;

        uint32_t tag=0;

        CacheLineData data {

        };

    };

    struct LLCRequest {

        // Aligned byte address, not a line index.
        uint32_t line_address=0;

        uint64_t transaction_id=0;

        int requester=0;

        bool write=false;

        CacheLineData data {

        };

        Cycle accepted=0;

    };

    struct LLCReply {

        // Aligned byte address, not a line index.
        uint32_t line_address=0;

        uint64_t transaction_id=0;

        int requester=0;

        CacheLineData data {

        };

    };

    struct LLCBank {

        std::vector<LLCEntry> entries;

        std::deque<LLCRequest> requests;

        std::deque<LLCReply> replies;

        std::optional<LLCRequest> active;

        std::string phase="idle";

        unsigned slot=0;

        Cycle ready=0;

        uint64_t memory_transaction_id=0;

    };

    struct SystemState {

        MeshNetwork network;

        std::vector<PrivateCache> caches;

        std::map<uint32_t,DirectoryEntry> directory;

        std::vector<MemoryOrderUnit> order;

        std::vector<std::optional<MemoryRequest>> cpu,cache_input;

        std::vector<std::deque<MemoryResult>> results,cache_results;

        std::vector<MemoryController> controllers;

        std::vector<LLCBank> llc;

    };

    struct ArbitrationCandidate {

        // Router input ID (port * 3 + protocol class), not a vector position.
        unsigned input=0;

        Packet packet;

    };

    enum class MemoryAction {

        Wait,Issue,BufferStore,Forward,CompleteFence
    };

    struct MemoryOrderDecision {

        MemoryAction kind=MemoryAction::Wait;

        uint32_t value=0;

    };

    struct RoutingContext {

        MeshCoordinate current_router,destination_router;

        unsigned router_tile_id=0;

    };

    class MulticoreSystem;


    int select_output_port(const RoutingContext& context, const SystemConfig& configuration);

    std::optional<unsigned> select_router_input(
        const std::vector<ArbitrationCandidate>& candidates,
        unsigned round_robin_cursor, const SystemConfig& configuration);

    MemoryOrderDecision decide_memory_action(
        const MemoryRequest& request, const MemoryOrderUnit& order_unit,
        const SystemConfig& configuration);

    std::optional<unsigned> select_memory_request(
        const std::vector<unsigned>& eligible_requests,
        const MemoryController& controller, Cycle cycle,
        const SystemConfig& configuration);

    // Return true when accepting the input; false leaves it unchanged for retry.
    bool accept_cache_request(MulticoreSystem& system, int core_id,
                              const MemoryRequest& request);
    bool handle_cache_message(MulticoreSystem& system, int core_id,
                              const Packet& packet);
    bool handle_directory_message(MulticoreSystem& system, const Packet& packet);
    void advance_directory_transaction(MulticoreSystem& system, uint32_t line_address);
    void count_event(MulticoreSystem& system, const std::string& component,
                     const std::string& event, uint64_t amount);
    void sample_occupancy_and_stalls(MulticoreSystem& system);

    class MulticoreSystem {

        public:  SystemConfig configuration;

        Cycle cycle=0;

        SystemState current,next;

        Counters counters,observer;

        std::vector<std::string> trace;

        explicit MulticoreSystem(SystemConfig);

        void reset();

        void begin();

        void evaluate_network_only();

        void evaluate();

        void commit();

        void tick();

        bool submit(const MemoryRequest&);

        std::optional<MemoryResult> result(int);

        bool idle()const;

        // Protocol building blocks; operate on NEXT state, never on CURRENT.
        bool can_send(int source_tile, MessageType type, unsigned count=1)const;

        bool send(Packet);

        Packet message(MessageType type, int source_tile, int destination_tile,
                       uint32_t line_address, uint64_t transaction_id,
                       const CacheLineData& data=CacheLineData {

        },int requester=-1)const;

        void cache_complete(int core_id, uint32_t result_value);

        void cpu_complete(int,const MemoryRequest&,uint32_t);

        void event(const std::string&,const std::string&,uint64_t=1);

        bool llc_request(int home_tile, const LLCRequest& request);

        std::optional<LLCReply> llc_reply(int home_tile, uint32_t line_address,
                                         uint64_t transaction_id, int requester);

        void log(const std::string&);

        MeshCoordinate coord(int)const;

        int tile(MeshCoordinate)const;

        int neighbor(int,int)const;

        int home(uint32_t line)const;

        int controller(uint32_t line)const;

        unsigned bank(uint32_t line)const;

        unsigned row(uint32_t line)const;

        uint32_t line_address(uint32_t a)const {

            return a&~uint32_t(63);

        }

        unsigned slot(uint32_t a)const {

            return (a/64)%configuration.cache_lines;

        }

        void initialize(uint32_t,uint32_t);

        void save(const std::string&)const;

        void check()const;

        private:  uint64_t packet_next=1,memory_next=uint64_t(1)<<48;

        bool begun=false;

        std::vector<uint32_t> memory;

        CacheLineData initial_line(uint32_t)const;

        uint32_t architectural_word(uint32_t)const;

        void network();

        void ordering();

        void endpoints();

        void dram();

        void llc();

        bool llc_memory_reply(const Packet&);

    };

    [[noreturn]] inline void not_implemented(const char* id) {

        throw std::runtime_error(std::string("UNIMPLEMENTED ")+id);

    }

}

