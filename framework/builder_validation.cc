#include "framework/internal.h"

namespace pipesim {

    using namespace framework_internal;

    namespace {

        int FindStageNode(const std::vector<NodeRecord>& nodes, StageRole role) {

            for (const NodeRecord& node : nodes) if (node.kind == NodeRecord::Kind::kStage && node.stage_role == role) return node.id;

            return -1;

        }

        int FindRegisterNode(const std::vector<NodeRecord>& nodes, RegisterRole role) {

            for (const NodeRecord& node : nodes) if (node.kind == NodeRecord::Kind::kRegister && node.register_role == role) return node.id;

            return -1;

        }

        int FindUnitNode(const std::vector<NodeRecord>& nodes, UnitRole role) {

            for (const NodeRecord& node : nodes) if (node.kind == NodeRecord::Kind::kUnit && node.unit_role == role) return node.id;

            return -1;

        }

        int FindCacheNode(const std::vector<NodeRecord>& nodes, cache::CacheRole role) {

            for (const NodeRecord& node : nodes) if (node.kind == NodeRecord::Kind::kCache && node.cache_role == role) return node.id;

            return -1;

        }

        bool HasEdge(const std::vector<EdgeRecord>& edges,int source,int destination,ConnectionType type) {

            for(const EdgeRecord& edge:edges)if(edge.source==source&&edge.destination==destination&&edge.type==type)return true;

            return false;

        }

        void Require(bool condition,const std::string& message,std::vector<std::string>* errors) {

            if(!condition)errors->push_back(message);

        }

    }

    std::vector<std::string> ValidateBuilder(const ProcessorBuilder::Impl& builder,                                          const Machine& machine) {

        std::vector<std::string> errors;

        const char* stage_names[] = {

            "IF", "ID", "EX", "MEM", "WB"
        };

        const char* register_names[] = {

            "PC", "IF_ID", "ID_EX", "EX_MEM", "MEM_WB"
        };

        std::array<int, 5> stage_counts = {

        };

        std::array<int, 6> register_counts = {

        };

        for (const NodeRecord& node : builder.nodes) {

            if (node.kind == NodeRecord::Kind::kStage) {

                const std::size_t index = static_cast<std::size_t>(node.stage_role);

                ++stage_counts[index];

                Require(node.name == stage_names[index],               "non-canonical stage name " + node.name, &errors);

            }

            else if (node.kind == NodeRecord::Kind::kRegister) {

                const std::size_t index = static_cast<std::size_t>(node.register_role);

                ++register_counts[index];

                if (index < 5) {

                    Require(node.name == register_names[index],                 "non-canonical register name " + node.name, &errors);

                }

            }

        }

        for (std::size_t index = 0; index < stage_counts.size(); ++index) {

            Require(stage_counts[index] == 1,             std::string("expected exactly one stage ") + stage_names[index],             &errors);

        }

        for (std::size_t index = 0; index < 5; ++index) {

            Require(register_counts[index] == 1,             std::string("expected exactly one register ") +                 register_names[index],             &errors);

        }

        const int fetch = FindStageNode(builder.nodes, StageRole::kFetch);

        const int decode = FindStageNode(builder.nodes, StageRole::kDecode);

        const int execute = FindStageNode(builder.nodes, StageRole::kExecute);

        const int memory = FindStageNode(builder.nodes, StageRole::kMemory);

        const int writeback = FindStageNode(builder.nodes, StageRole::kWriteback);

        const int pc = FindRegisterNode(builder.nodes, RegisterRole::kProgramCounter);

        const int if_id = FindRegisterNode(builder.nodes, RegisterRole::kIfId);

        const int id_ex = FindRegisterNode(builder.nodes, RegisterRole::kIdEx);

        const int ex_mem = FindRegisterNode(builder.nodes, RegisterRole::kExMem);

        const int mem_wb = FindRegisterNode(builder.nodes, RegisterRole::kMemWb);

        const int main_control =       FindUnitNode(builder.nodes, UnitRole::kMainControl);

        const int hazard = FindUnitNode(builder.nodes, UnitRole::kHazardControl);

        const int branch_prediction =       FindUnitNode(builder.nodes, UnitRole::kBranchPrediction);

        const int speculation_control =       FindUnitNode(builder.nodes, UnitRole::kSpeculationControl);

        Require(fetch >= 0, "missing IF stage", &errors);

        Require(decode >= 0, "missing ID stage", &errors);

        Require(execute >= 0, "missing EX stage", &errors);

        Require(memory >= 0, "missing MEM stage", &errors);

        Require(writeback >= 0, "missing WB stage", &errors);

        Require(pc >= 0, "missing PC state", &errors);

        Require(if_id >= 0, "missing IF/ID register", &errors);

        Require(id_ex >= 0, "missing ID/EX register", &errors);

        Require(ex_mem >= 0, "missing EX/MEM register", &errors);

        Require(mem_wb >= 0, "missing MEM/WB register", &errors);

        Require(main_control >= 0, "missing main control unit", &errors);

        Require(hazard >= 0, "missing hazard control unit", &errors);

        Require(branch_prediction >= 0, "missing branch prediction unit", &errors);

        Require(speculation_control >= 0, "missing speculation control unit", &errors);

        const UnitRole execution_roles[] = {

            UnitRole::kAdd, UnitRole::kSub, UnitRole::kAnd, UnitRole::kOr,       UnitRole::kXor, UnitRole::kControl, UnitRole::kAgu
        };

        const char* execution_names[] = {

            "ADD", "SUB", "AND", "OR", "XOR", "CONTROL", "AGU"
        };

        std::array<int, 7> execution_nodes = {

        };

        for (std::size_t index = 0; index < execution_nodes.size(); ++index) {

            execution_nodes[index] = FindUnitNode(builder.nodes, execution_roles[index]);

            Require(execution_nodes[index] >= 0,             std::string("missing execution unit ") + execution_names[index],             &errors);

        }

        std::array<int, 12> unit_counts = {

        };

        std::set<const TrackedUnit*> unit_addresses;

        for (const UnitEntry& entry : builder.units) {

            ++unit_counts[static_cast<std::size_t>(entry.role)];

            Require(unit_addresses.insert(entry.unit.get()).second,             "one unit object was registered more than once", &errors);

            const std::size_t role_index = static_cast<std::size_t>(entry.role);

            if (role_index < 7) {

                const ExecutionPipeline* pipeline =           dynamic_cast<const ExecutionPipeline*>(entry.unit.get());

                Require(pipeline != nullptr,               entry.name + " is not a real ExecutionPipeline", &errors);

                Require(entry.name == execution_names[role_index],               "non-canonical execution unit name " + entry.name, &errors);

                if (pipeline != nullptr) {

                    const ExecutionUnitKind kind =             static_cast<ExecutionUnitKind>(role_index);

                    const ExecutionUnitConfig& expected =             machine.Configuration().execution_units[role_index];

                    Require(pipeline->kind() == kind,                 entry.name + " execution kind mismatch", &errors);

                    Require(pipeline->config().latency == expected.latency &&                     pipeline->config().initiation_interval ==                         expected.initiation_interval,                 entry.name + " execution configuration mismatch", &errors);

                }

            }

            else if (entry.role == UnitRole::kMainControl) {

                Require(entry.name == "MAIN_CONTROL",               "non-canonical main-control name", &errors);

            }

            else if (entry.role == UnitRole::kHazardControl) {

                Require(entry.name == "HAZARD_CONTROL",               "non-canonical hazard-control name", &errors);

            }

            else if (entry.role == UnitRole::kBranchPrediction) {

                Require(entry.name == "BRANCH_PREDICTION",               "non-canonical branch-prediction name", &errors);

            }

            else if (entry.role == UnitRole::kSpeculationControl) {

                Require(entry.name == "SPECULATION_CONTROL",               "non-canonical speculation-control name", &errors);

            }

        }

        for (std::size_t index = 0; index < 11; ++index) {

            Require(unit_counts[index] == 1,             std::string("expected exactly one unit role ") +                 UnitRoleName(static_cast<UnitRole>(index)),             &errors);

        }

        std::array<int, 4> cache_counts = {

        };

        for (const CacheEntry& entry : builder.caches) {

            ++cache_counts[static_cast<std::size_t>(entry.role)];

            Require(entry.name == cache::CacheRoleName(entry.role),             "non-canonical cache name " + entry.name, &errors);

        }

        for (std::size_t i = 0; i < cache_counts.size(); ++i) {

            Require(cache_counts[i] == 1,             std::string("expected exactly one ") +                 cache::CacheRoleName(static_cast<cache::CacheRole>(i)) +                 " controller",             &errors);

        }

        if (fetch >= 0 && decode >= 0 && execute >= 0 && memory >= 0 &&       writeback >= 0 && pc >= 0 && if_id >= 0 && id_ex >= 0 && ex_mem >= 0 &&       mem_wb >= 0 && main_control >= 0 && hazard >= 0) {

            Require(HasEdge(builder.edges, pc, fetch, ConnectionType::kData),             "missing PC -> IF data connection", &errors);

            Require(HasEdge(builder.edges, fetch, if_id, ConnectionType::kClockedData),             "missing IF -> IF/ID connection", &errors);

            Require(HasEdge(builder.edges, if_id, decode, ConnectionType::kData),             "missing IF/ID -> ID connection", &errors);

            Require(HasEdge(builder.edges, decode, id_ex, ConnectionType::kClockedData),             "missing ID -> ID/EX connection", &errors);

            Require(HasEdge(builder.edges, id_ex, execute, ConnectionType::kData),             "missing ID/EX -> EX connection", &errors);

            Require(HasEdge(builder.edges, execute, ex_mem, ConnectionType::kClockedData),             "missing EX -> EX/MEM connection", &errors);

            Require(HasEdge(builder.edges, ex_mem, memory, ConnectionType::kData),             "missing EX/MEM -> MEM connection", &errors);

            Require(HasEdge(builder.edges, memory, mem_wb, ConnectionType::kClockedData),             "missing MEM -> MEM/WB connection", &errors);

            Require(HasEdge(builder.edges, mem_wb, writeback, ConnectionType::kData),             "missing MEM/WB -> WB connection", &errors);

            for (std::size_t index = 0; index < execution_nodes.size(); ++index) {

                const int unit = execution_nodes[index];

                if (unit < 0) continue;

                Require(HasEdge(builder.edges, decode, unit,                       ConnectionType::kClockedData),               std::string("ID must dispatch to ") + execution_names[index],               &errors);

                Require(HasEdge(builder.edges, unit,                       index == static_cast<std::size_t>(ExecutionUnitKind::kAgu)                           ? memory                           : execute,                       ConnectionType::kData),               std::string(execution_names[index]) +                   " lacks its real result path",               &errors);

            }

            Require(HasEdge(builder.edges, main_control, decode,                     ConnectionType::kControl),             "main control must feed ID or ID/EX", &errors);

            Require(HasEdge(builder.edges, hazard, pc, ConnectionType::kStall),             "hazard control must be able to stall PC", &errors);

            Require(HasEdge(builder.edges, hazard, if_id, ConnectionType::kStall),             "hazard control must be able to stall IF/ID", &errors);

            Require(HasEdge(builder.edges, hazard, id_ex, ConnectionType::kBubble),             "hazard control must be able to bubble ID/EX", &errors);

            Require(HasEdge(builder.edges, branch_prediction, fetch,                     ConnectionType::kControl),             "branch prediction must feed IF", &errors);

            Require(HasEdge(builder.edges, speculation_control, fetch,                     ConnectionType::kStall),             "speculation control must be able to stall IF", &errors);

            Require(HasEdge(builder.edges, speculation_control, decode,                     ConnectionType::kStall),             "speculation control must be able to stall ID", &errors);

            if (execution_nodes[static_cast<std::size_t>(             ExecutionUnitKind::kControl)] >= 0) {

                Require(HasEdge(builder.edges,                       execution_nodes[static_cast<std::size_t>(                           ExecutionUnitKind::kControl)],                       pc, ConnectionType::kRedirect),               "CONTROL must be able to redirect PC", &errors);

            }

            const int l1i = FindCacheNode(builder.nodes, cache::CacheRole::kL1I);

            const int l2i = FindCacheNode(builder.nodes, cache::CacheRole::kL2I);

            const int l1d = FindCacheNode(builder.nodes, cache::CacheRole::kL1D);

            const int l2d = FindCacheNode(builder.nodes, cache::CacheRole::kL2D);

            if (l1i >= 0 && l2i >= 0 && l1d >= 0 && l2d >= 0) {

                Require(HasEdge(builder.edges, fetch, l1i, ConnectionType::kData),               "IF must feed L1I", &errors);

                Require(HasEdge(builder.edges, l1i, l2i, ConnectionType::kData),               "L1I must feed L2I", &errors);

                Require(HasEdge(builder.edges, memory, l1d, ConnectionType::kData),               "AGU/MEM path must feed L1D", &errors);

                Require(HasEdge(builder.edges, l1d, l2d, ConnectionType::kData),               "L1D must feed L2D", &errors);

            }

        }

        return errors;

    }

}

// namespace pipesim
