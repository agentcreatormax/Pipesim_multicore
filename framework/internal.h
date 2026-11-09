#pragma once
#include "pipesim.h"
#include "cache/cache_controller.h"
#include "execution_unit.h"
#include "framework/simple_memory.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pipesim {

    namespace framework_internal {

        constexpr int kRegisterCount = 8;

        inline std::string Trim(const std::string& text) {

            std::size_t begin = 0;

            while (begin < text.size() &&          std::isspace(static_cast<unsigned char>(text[begin]))) {

                ++begin;

            }

            std::size_t end = text.size();

            while (end > begin &&          std::isspace(static_cast<unsigned char>(text[end - 1]))) {

                --end;

            }

            return text.substr(begin, end - begin);

        }

        inline bool ParseUnsigned(const std::string& text, std::uint32_t* value) {

            try {

                std::size_t used = 0;

                const unsigned long long parsed = std::stoull(text, &used, 0);

                if (used != text.size() ||         parsed > std::numeric_limits<std::uint32_t>::max()) {

                    return false;

                }

                *value = static_cast<std::uint32_t>(parsed);

                return true;

            }

            catch (...) {

                return false;

            }

        }

        inline bool ParseSigned(const std::string& text, std::int32_t* value) {

            try {

                std::size_t used = 0;

                const long long parsed = std::stoll(text, &used, 0);

                if (used != text.size() ||         parsed < std::numeric_limits<std::int32_t>::min() ||         parsed > std::numeric_limits<std::int32_t>::max()) {

                    return false;

                }

                *value = static_cast<std::int32_t>(parsed);

                return true;

            }

            catch (...) {

                return false;

            }

        }

        inline bool ParseOpcode(const std::string& text, Opcode* opcode) {

            static const std::map<std::string, Opcode> kOpcodes = {

                {

                    "NOP", Opcode::kNop
                },   {

                    "ADD", Opcode::kAdd
                },       {

                    "SUB", Opcode::kSub
                },   {

                    "AND", Opcode::kAnd
                },       {

                    "OR", Opcode::kOr
                },     {

                    "XOR", Opcode::kXor
                },       {

                    "ADDI", Opcode::kAddi
                }, {

                    "LW", Opcode::kLw
                },       {

                    "SW", Opcode::kSw
                },     {

                    "BEQ", Opcode::kBeq
                },       {

                    "BNE", Opcode::kBne
                },   {

                    "J", Opcode::kJ
                },       {

                    "HALT", Opcode::kHalt
                }, {

                    "FENCE", Opcode::kFence
                }, {

                    "XCHG", Opcode::kXchg
                },
            };

            const auto found = kOpcodes.find(text);

            if (found == kOpcodes.end()) return false;

            *opcode = found->second;

            return true;

        }

        inline bool HasExtra(std::istringstream* fields) {

            std::string extra;

            return static_cast<bool>(*fields >> extra);

        }

        inline const char* StageRoleName(StageRole role) {

            switch (role) {

                case StageRole::kFetch: return "IF";

                case StageRole::kDecode: return "ID";

                case StageRole::kExecute: return "EX";

                case StageRole::kMemory: return "MEM";

                case StageRole::kWriteback: return "WB";

            }

            return "UNKNOWN";

        }

        inline const char* RegisterRoleName(RegisterRole role) {

            switch (role) {

                case RegisterRole::kProgramCounter: return "PC";

                case RegisterRole::kIfId: return "IF_ID";

                case RegisterRole::kIdEx: return "ID_EX";

                case RegisterRole::kExMem: return "EX_MEM";

                case RegisterRole::kMemWb: return "MEM_WB";

                case RegisterRole::kCustom: return "CUSTOM";

            }

            return "UNKNOWN";

        }

        inline const char* UnitRoleName(UnitRole role) {

            switch (role) {

                case UnitRole::kAdd: return "ADD";

                case UnitRole::kSub: return "SUB";

                case UnitRole::kAnd: return "AND";

                case UnitRole::kOr: return "OR";

                case UnitRole::kXor: return "XOR";

                case UnitRole::kControl: return "CONTROL";

                case UnitRole::kAgu: return "AGU";

                case UnitRole::kMainControl: return "MAIN_CONTROL";

                case UnitRole::kHazardControl: return "HAZARD_CONTROL";

                case UnitRole::kBranchPrediction: return "BRANCH_PREDICTION";

                case UnitRole::kSpeculationControl: return "SPECULATION_CONTROL";

                case UnitRole::kCustom: return "CUSTOM";

            }

            return "UNKNOWN";

        }

        inline const char* ConnectionTypeName(ConnectionType type) {

            switch (type) {

                case ConnectionType::kData: return "DATA";

                case ConnectionType::kClockedData: return "CLOCKED_DATA";

                case ConnectionType::kControl: return "CONTROL";

                case ConnectionType::kStall: return "STALL";

                case ConnectionType::kBubble: return "BUBBLE";

                case ConnectionType::kRedirect: return "REDIRECT";

            }

            return "UNKNOWN";

        }

        inline bool SameInstruction(const Instruction& first, const Instruction& second) {

            return first.opcode == second.opcode && first.pc == second.pc &&          first.rd == second.rd && first.rs1 == second.rs1 &&          first.rs2 == second.rs2 && first.immediate == second.immediate;

        }

        inline bool OpcodeWritesRegister(Opcode opcode) {

            return opcode == Opcode::kAdd || opcode == Opcode::kAddi ||          opcode == Opcode::kSub || opcode == Opcode::kAnd ||          opcode == Opcode::kOr || opcode == Opcode::kXor ||          opcode == Opcode::kLw || opcode == Opcode::kXchg;

        }

        inline bool IsMemoryOpcode(Opcode opcode) {

            return opcode == Opcode::kLw || opcode == Opcode::kSw || opcode == Opcode::kFence || opcode == Opcode::kXchg;

        }

        inline bool IsFixedOpcode(Opcode opcode) {

            return opcode != Opcode::kNop && !IsMemoryOpcode(opcode);

        }

    }

    class Machine::Impl {

        public:   enum class FetchState {

            kFree, kPending, kReady, kStalePending
        };

        struct FetchSlot {

            FetchState state = FetchState::kFree;

            std::uint64_t request_id = 0;

            std::uint64_t sequence = 0;

            std::uint32_t generation = 0;

            std::uint32_t address = 0;

            std::uint32_t token = 0;

            std::uint64_t fetch_cycle = 0;

            BranchPredictionSet predictions;

        };

        struct PendingData {

            bool valid = false;

            std::uint64_t request_id = 0;

            std::uint64_t sequence = 0;

            bool write = false;

            std::uint32_t address = 0;

        };

        struct Completion {

            std::uint64_t sequence = 0;

            Instruction instruction;

            bool writes_register = false;

            bool ready = false;

            std::uint32_t value = 0;

            bool halt = false;

            std::optional<std::uint64_t> normal_result_boundary;

        };

        cache::SystemConfig config;

        bool configured = false;

        bool program_loaded = false;

        std::array<std::uint32_t, framework_internal::kRegisterCount> initial_registers = {

        };

        std::array<std::uint32_t, framework_internal::kRegisterCount> registers = {

        };

        std::uint32_t entry_point = 0;

        std::unordered_map<std::uint32_t, Instruction> token_to_instruction;

        std::set<std::uint32_t> instruction_pcs;

        std::function<bool(uint64_t,Opcode,uint32_t,uint32_t)> external_submit;

        std::function<std::optional<std::pair<uint64_t,uint32_t>>()> external_poll;

        framework::ByteImage instruction_image;

        framework::ByteImage data_image;

        std::unique_ptr<framework::SimpleMemory> instruction_memory;

        std::unique_ptr<framework::SimpleMemory> data_memory;

        cache::TransactionIdService transaction_ids;

        std::unique_ptr<cache::CacheController> l1i;

        std::unique_ptr<cache::CacheController> l2i;

        std::unique_ptr<cache::CacheController> l1d;

        std::unique_ptr<cache::CacheController> l2d;

        cache::RegisteredLink<cache::CacheRequest> cpu_l1i_request;

        cache::RegisteredLink<cache::CacheRequest> l1i_l2i_request;

        cache::RegisteredLink<cache::CacheRequest> l2i_memory_request;

        cache::RegisteredLink<cache::CacheResponse> l1i_cpu_response;

        cache::RegisteredLink<cache::CacheResponse> l2i_l1i_response;

        cache::RegisteredLink<cache::CacheResponse> memory_l2i_response;

        cache::RegisteredLink<cache::CacheRequest> cpu_l1d_request;

        cache::RegisteredLink<cache::CacheRequest> l1d_l2d_request;

        cache::RegisteredLink<cache::CacheRequest> l2d_memory_request;

        cache::RegisteredLink<cache::CacheResponse> l1d_cpu_response;

        cache::RegisteredLink<cache::CacheResponse> l2d_l1d_response;

        cache::RegisteredLink<cache::CacheResponse> memory_l2d_response;

        std::vector<FetchSlot> fetch_current;

        std::vector<FetchSlot> fetch_next;

        std::vector<PendingData> data_current;

        std::vector<PendingData> data_next;

        std::deque<Completion> completion_current;

        std::deque<Completion> completion_next;

        std::vector<std::optional<std::uint64_t>> reservation_current;

        std::vector<std::optional<std::uint64_t>> reservation_next;

        std::uint64_t current_boundary = 0;

        std::uint64_t next_fetch_sequence_current = 0;

        std::uint64_t next_fetch_sequence_next = 0;

        std::uint32_t fetch_generation_current = 0;

        std::uint32_t fetch_generation_next = 0;

        bool issued_fetch_this_cycle = false;

        bool issued_data_this_cycle = false;

        bool retirement_planned = false;

        bool normal_completion_seen = false;

        std::array<bool, 3> stall_point_used = {

        };

        bool data_stall_recorded = false;

        bool halted = false;

        std::uint64_t retired = 0;

        std::uint64_t data_stalls = 0;

        std::uint64_t control_stalls = 0;

        std::uint64_t icache_input_stalls = 0;

        std::uint64_t dcache_input_stalls = 0;

        std::uint64_t outstanding_raw_stalls = 0;

        std::uint64_t outstanding_waw_stalls = 0;

        std::uint64_t fetch_queue_full_stalls = 0;

        std::uint64_t pending_data_full_stalls = 0;

        std::uint64_t completion_queue_full_stalls = 0;

        std::uint64_t execution_unit_busy_stalls = 0;

        std::uint64_t execution_completion_slot_stalls = 0;

        std::uint64_t stale_ifetch_responses = 0;

        void RequireReady() const {

            if (!configured) throw std::runtime_error("configuration is not loaded");

            if (!program_loaded) throw std::runtime_error("program is not loaded");

        }

        void BeginLinks() {

            cpu_l1i_request.BeginCycle();

            l1i_l2i_request.BeginCycle();

            l2i_memory_request.BeginCycle();

            l1i_cpu_response.BeginCycle();

            l2i_l1i_response.BeginCycle();

            memory_l2i_response.BeginCycle();

            cpu_l1d_request.BeginCycle();

            l1d_l2d_request.BeginCycle();

            l2d_memory_request.BeginCycle();

            l1d_cpu_response.BeginCycle();

            l2d_l1d_response.BeginCycle();

            memory_l2d_response.BeginCycle();

        }

        void CommitLinks() {

            cpu_l1i_request.Commit();

            l1i_l2i_request.Commit();

            l2i_memory_request.Commit();

            l1i_cpu_response.Commit();

            l2i_l1i_response.Commit();

            memory_l2i_response.Commit();

            cpu_l1d_request.Commit();

            l1d_l2d_request.Commit();

            l2d_memory_request.Commit();

            l1d_cpu_response.Commit();

            l2d_l1d_response.Commit();

            memory_l2d_response.Commit();

        }

        void ResetLinks() {

            cpu_l1i_request.Reset();

            l1i_l2i_request.Reset();

            l2i_memory_request.Reset();

            l1i_cpu_response.Reset();

            l2i_l1i_response.Reset();

            memory_l2i_response.Reset();

            cpu_l1d_request.Reset();

            l1d_l2d_request.Reset();

            l2d_memory_request.Reset();

            l1d_cpu_response.Reset();

            l2d_l1d_response.Reset();

            memory_l2d_response.Reset();

        }

        FetchSlot* FindFetch(std::vector<FetchSlot>* slots, std::uint64_t id) {

            for (FetchSlot& slot : *slots) {

                if (slot.state != FetchState::kFree && slot.request_id == id) return &slot;

            }

            return nullptr;

        }

        PendingData* FindData(std::vector<PendingData>* slots, std::uint64_t id) {

            for (PendingData& slot : *slots) {

                if (slot.valid && slot.request_id == id) return &slot;

            }

            return nullptr;

        }

        Completion* FindCompletion(std::deque<Completion>* entries,                              std::uint64_t sequence) {

            for (Completion& entry : *entries) {

                if (entry.sequence == sequence) return &entry;

            }

            return nullptr;

        }

        const Completion* FindCompletion(const std::deque<Completion>& entries,                                    std::uint64_t sequence) const {

            for (const Completion& entry : entries) {

                if (entry.sequence == sequence) return &entry;

            }

            return nullptr;

        }

        std::size_t ReservationNextIndex(std::uint64_t result_boundary) const {

            if (result_boundary < current_boundary + 2U) {

                throw std::runtime_error("normal result boundary is too early");

            }

            const std::uint64_t index = result_boundary - current_boundary - 2U;

            if (index >= reservation_current.size()) {

                throw std::runtime_error("normal result boundary exceeds calendar");

            }

            return static_cast<std::size_t>(index);

        }

        void ConsumeInstructionResponse() {

            if (!l1i_cpu_response.HasValue()) return;

            const cache::CacheResponse& response = l1i_cpu_response.Value();

            cache::ValidateResponsePayload(response);

            if (response.type != cache::ResponseType::kReadData || response.size != 4) {

                throw std::runtime_error("CPU instruction port received a non-word read response");

            }

            FetchSlot* slot = FindFetch(&fetch_next, response.id);

            if (slot == nullptr || slot->address != response.address ||         slot->sequence != response.sequence ||         slot->generation != response.generation ||         response.origin_id != response.id) {

                throw std::runtime_error("CPU instruction response has no exact owner");

            }

            if (slot->state == FetchState::kPending &&         slot->generation == fetch_generation_next) {

                slot->token = cache::BytesToWord(response.data);

                slot->state = FetchState::kReady;

            }

            else if (slot->state == FetchState::kStalePending ||                slot->generation != fetch_generation_next) {

                if (stale_ifetch_responses ==           std::numeric_limits<std::uint64_t>::max()) {

                    throw std::runtime_error("counter overflow: stale_ifetch_responses");

                }

                cache::CheckedIncrement(&stale_ifetch_responses,                               "stale_ifetch_responses");

                *slot = FetchSlot();

            }

            else {

                throw std::runtime_error("duplicate CPU instruction response");

            }

            l1i_cpu_response.PlanDequeue();

        }

        void ConsumeDataResponse() {

            if (!l1d_cpu_response.HasValue()) return;

            const cache::CacheResponse& response = l1d_cpu_response.Value();

            cache::ValidateResponsePayload(response);

            PendingData* pending = FindData(&data_next, response.id);

            if (pending == nullptr || response.origin_id != response.id ||         response.sequence != pending->sequence ||         response.address != pending->address || response.size != 4) {

                throw std::runtime_error("CPU data response has no exact owner");

            }

            const cache::ResponseType expected = pending->write         ? cache::ResponseType::kWriteAck         : cache::ResponseType::kReadData;

            if (response.type != expected) {

                throw std::runtime_error("CPU data response type mismatch");

            }

            const Completion* current_completion =         FindCompletion(completion_current, pending->sequence);

            Completion* completion = FindCompletion(&completion_next,                                             pending->sequence);

            if (current_completion == nullptr || current_completion->ready ||         completion == nullptr || completion->ready ||         (pending->write && current_completion->instruction.opcode !=                                Opcode::kSw) ||         (!pending->write && current_completion->instruction.opcode !=                                 Opcode::kLw)) {

                throw std::runtime_error("CPU data completion owner mismatch");

            }

            completion->value = pending->write ? 0 : cache::BytesToWord(response.data);

            completion->ready = true;

            pending->valid = false;

            l1d_cpu_response.PlanDequeue();

        }

        void OperateSide(       std::uint64_t cycle, cache::CacheController* first,       cache::CacheController* second, framework::SimpleMemory* memory,       cache::RegisteredLink<cache::CacheRequest>* cpu_request,       cache::RegisteredLink<cache::CacheRequest>* first_request,       cache::RegisteredLink<cache::CacheRequest>* memory_request,       cache::RegisteredLink<cache::CacheResponse>* cpu_response,       cache::RegisteredLink<cache::CacheResponse>* first_response,       cache::RegisteredLink<cache::CacheResponse>* memory_response) {

            const std::optional<cache::CacheResponse> first_offer =         first->ResponseIntent();

            const bool first_response_grant =         first_offer.has_value() && cpu_response->CanEnqueue();

            if (first_response_grant) cpu_response->PlanEnqueue(*first_offer);

            const cache::CacheResponse* first_lower_response =         first_response->HasValue() ? &first_response->Value() : nullptr;

            const bool first_lower_response_grant = first_lower_response != nullptr;

            if (first_lower_response_grant) first_response->PlanDequeue();

            const std::optional<cache::CacheResponse> second_offer =         second->ResponseIntent();

            const bool second_response_grant =         second_offer.has_value() && first_response->CanEnqueue();

            if (second_response_grant) first_response->PlanEnqueue(*second_offer);

            const cache::CacheResponse* second_lower_response =         memory_response->HasValue() ? &memory_response->Value() : nullptr;

            const bool second_lower_response_grant = second_lower_response != nullptr;

            if (second_lower_response_grant) memory_response->PlanDequeue();

            const framework::MemoryIntent memory_intent = memory->Inspect(cycle);

            const bool memory_response_grant =         memory_intent.response.has_value() && memory_response->CanEnqueue();

            if (memory_response_grant) {

                memory_response->PlanEnqueue(*memory_intent.response);

            }

            const bool memory_request_grant =         memory_request->HasValue() && memory_intent.can_accept_request;

            if (memory_request_grant) memory_request->PlanDequeue();

            const std::optional<cache::CacheRequest> second_request_offer =         second->LowerRequestIntent();

            const bool second_lower_queue_available = memory_request->CanEnqueue();

            const bool second_request_grant =         second_request_offer.has_value() && second_lower_queue_available;

            if (second_request_grant) {

                memory_request->PlanEnqueue(*second_request_offer);

            }

            const cache::CacheRequest* second_upper_request =         first_request->HasValue() ? &first_request->Value() : nullptr;

            const cache::CacheStageIntent second_stage = second->StageIntent(         second_upper_request, second_lower_response, second_response_grant,         second_request_grant);

            if (second_stage.accept_lower_response !=         second_lower_response_grant) {

                throw std::runtime_error("L2 lower-response intent is inconsistent");

            }

            const bool second_upper_request_grant =         first_request->HasValue() && second_stage.accept_upper_request;

            if (second_upper_request_grant) first_request->PlanDequeue();

            const std::optional<cache::CacheRequest> first_request_offer =         first->LowerRequestIntent();

            const bool first_lower_queue_available = first_request->CanEnqueue();

            const bool first_request_grant =         first_request_offer.has_value() && first_lower_queue_available;

            if (first_request_grant) first_request->PlanEnqueue(*first_request_offer);

            const cache::CacheRequest* first_upper_request =         cpu_request->HasValue() ? &cpu_request->Value() : nullptr;

            const cache::CacheStageIntent first_stage = first->StageIntent(         first_upper_request, first_lower_response, first_response_grant,         first_request_grant);

            if (first_stage.accept_lower_response != first_lower_response_grant) {

                throw std::runtime_error("L1 lower-response intent is inconsistent");

            }

            const bool first_upper_request_grant =         cpu_request->HasValue() && first_stage.accept_upper_request;

            if (first_upper_request_grant) cpu_request->PlanDequeue();

            cache::CacheOperateGrants first_grants;

            first_grants.upper_request = first_upper_request_grant;

            first_grants.lower_response = first_lower_response_grant;

            first_grants.upper_response = first_response_grant;

            first_grants.lower_request = first_request_grant;

            first_grants.lower_request_queue_available =         first_lower_queue_available;

            first->Operate(cycle,                    cpu_request->HasValue() ? &cpu_request->Value() : nullptr,                    first_lower_response, first_grants);

            cache::CacheOperateGrants second_grants;

            second_grants.upper_request = second_upper_request_grant;

            second_grants.lower_response = second_lower_response_grant;

            second_grants.upper_response = second_response_grant;

            second_grants.lower_request = second_request_grant;

            second_grants.lower_request_queue_available =         second_lower_queue_available;

            second->Operate(cycle,                     first_request->HasValue() ? &first_request->Value() : nullptr,                     second_lower_response, second_grants);

            memory->Operate(         cycle,         memory_request_grant ? &memory_request->Value() : nullptr,         memory_response_grant);

        }

        std::vector<std::uint8_t> ArchitecturalDataImage() const {

            std::vector<std::uint8_t> bytes = data_image.bytes();

            auto overlay = [&](std::uint32_t address,                        const std::vector<std::uint8_t>& data) {

                if (address >= config.data_memory.image_bytes) return;

                const std::size_t count = std::min<std::size_t>(           data.size(), config.data_memory.image_bytes - address);

                std::copy(data.begin(), data.begin() + count, bytes.begin() + address);

            };

            auto overlay_cache = [&](const cache::CacheController* controller) {

                if (controller == nullptr) return;

                const cache::CacheSnapshot snapshot = controller->Snapshot();

                const cache::CacheConfig& geometry = controller->config();

                std::vector<cache::WritebackSnapshot> writebacks;

                for (const cache::WritebackSnapshot& item : snapshot.writebacks) {

                    if (item.valid &&             (item.state == "UNSENT" || item.state == "IN_FLIGHT")) {

                        writebacks.push_back(item);

                    }

                }

                std::sort(writebacks.begin(), writebacks.end(),                 [](const cache::WritebackSnapshot& first,                    const cache::WritebackSnapshot& second) {

                    return std::tie(first.arrival_order, first.lower_id) <                std::tie(second.arrival_order, second.lower_id);
                });

                for (const cache::WritebackSnapshot& item : writebacks) {

                    overlay(item.address, item.data);

                }

                for (const cache::LineSnapshot& item : snapshot.lines) {

                    if (!item.line.valid || !item.line.dirty) continue;

                    const std::uint64_t block =             (static_cast<std::uint64_t>(item.line.tag) * geometry.sets +              item.set) * geometry.line_size;

                    if (block <= std::numeric_limits<std::uint32_t>::max()) {

                        overlay(static_cast<std::uint32_t>(block), item.line.data);

                    }

                }

            };

            overlay_cache(l2d.get());

            overlay_cache(l1d.get());

            return bytes;

        }

    };

    struct NodeRecord {

        enum class Kind {

            kStage, kRegister, kUnit, kCache
        };

        int id = -1;

        Kind kind = Kind::kStage;

        std::string name;

        StageRole stage_role = StageRole::kFetch;

        RegisterRole register_role = RegisterRole::kCustom;

        UnitRole unit_role = UnitRole::kCustom;

        cache::CacheRole cache_role = cache::CacheRole::kL1I;

    };

    struct EdgeRecord {

        int source = -1;

        int destination = -1;

        ConnectionType type = ConnectionType::kData;

    };

    struct StageEntry {

        StageRole role;

        std::string name;

        std::unique_ptr<StageComponent> component;

    };

    struct RegisterEntry {

        RegisterRole role;

        std::string name;

        std::unique_ptr<ClockedState> state;

    };

    struct UnitEntry {

        UnitRole role;

        std::string name;

        std::unique_ptr<TrackedUnit> unit;

    };

    struct CacheEntry {

        cache::CacheRole role;

        std::string name;

        std::unique_ptr<cache::CacheController> controller;

    };

    class ProcessorBuilder::Impl {

        public:    int next_id = 0;

        std::vector<NodeRecord> nodes;

        std::vector<EdgeRecord> edges;

        std::vector<StageEntry> stages;

        std::vector<RegisterEntry> registers;

        std::vector<UnitEntry> units;

        std::vector<CacheEntry> caches;

    };

    class Processor::Impl {

        public:    Machine* machine = nullptr;

        std::vector<NodeRecord> nodes;

        std::vector<EdgeRecord> edges;

        std::vector<StageEntry> stages;

        std::vector<RegisterEntry> registers;

        std::vector<UnitEntry> units;

        std::uint64_t cycles = 0;

        StageEntry* FindStage(StageRole role) {

            for (StageEntry& stage : stages) if (stage.role == role) return &stage;

            return nullptr;

        }

        const StageEntry* FindStage(StageRole role) const {

            for (const StageEntry& stage : stages) if (stage.role == role) return &stage;

            return nullptr;

        }

        UnitEntry* FindUnit(UnitRole role) {

            for (UnitEntry& unit : units) if (unit.role == role) return &unit;

            return nullptr;

        }

        const UnitEntry* FindUnit(UnitRole role) const {

            for (const UnitEntry& unit : units) if (unit.role == role) return &unit;

            return nullptr;

        }

    };

    std::vector<std::string> ValidateBuilder(const ProcessorBuilder::Impl&, const Machine&);

}

// namespace pipesim
