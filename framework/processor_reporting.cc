#include "framework/internal.h"
#include "branch_prediction.h"

namespace pipesim {

    using namespace framework_internal;

    void Processor::PrintPipeline(std::ostream& output) const {

        output << "BEGIN PIPELINE\ncycle " << impl_->cycles << "\n";

        const StageRole order[] = {

            StageRole::kFetch, StageRole::kDecode,                              StageRole::kExecute, StageRole::kMemory,                              StageRole::kWriteback
        };

        for (StageRole role : order) {

            const StageSnapshot snapshot = impl_->FindStage(role)->component->Snapshot();

            output << StageRoleName(role) << " valid " << (snapshot.valid ? 1 : 0)            << " pc 0x" << std::hex << std::setw(8) << std::setfill('0')            << snapshot.pc << std::dec << std::setfill(' ') << " opcode "            << snapshot.opcode << " action " << snapshot.action << "\n";

        }

        for (std::size_t index = 0;        index < impl_->machine->impl_->fetch_current.size(); ++index) {

            const Machine::Impl::FetchSlot& slot =         impl_->machine->impl_->fetch_current[index];

            const char* state = "FREE";

            if (slot.state == Machine::Impl::FetchState::kPending) state = "PENDING";

            if (slot.state == Machine::Impl::FetchState::kReady) state = "READY";

            if (slot.state == Machine::Impl::FetchState::kStalePending) {

                state = "STALE_PENDING";

            }

            output << "FETCH_OWNER " << index << " " << state << " id "            << slot.request_id << " sequence " << slot.sequence            << " generation " << slot.generation << " address 0x" << std::hex            << slot.address << std::dec << " token " << slot.token << "\n";

        }

        for (std::size_t index = 0;        index < impl_->machine->impl_->data_current.size(); ++index) {

            const Machine::Impl::PendingData& entry =         impl_->machine->impl_->data_current[index];

            output << "DATA_OWNER " << index << " "            << (entry.valid ? "VALID" : "EMPTY") << " id "            << entry.request_id << " sequence " << entry.sequence << " write "            << (entry.write ? 1 : 0) << " address 0x" << std::hex            << entry.address << std::dec << "\n";

        }

        output << "COMPLETION_QUEUE size "          << impl_->machine->impl_->completion_current.size() << " capacity "          << impl_->machine->impl_->config.cpu.completion_queue_entries << "\n";

        for (std::size_t index = 0;        index < impl_->machine->impl_->completion_current.size(); ++index) {

            const Machine::Impl::Completion& entry =         impl_->machine->impl_->completion_current[index];

            output << "  entry " << index << " sequence " << entry.sequence            << " pc 0x" << std::hex << entry.instruction.pc << std::dec            << " opcode " << OpcodeName(entry.instruction.opcode) << " write "            << (entry.writes_register ? 1 : 0) << " ready "            << (entry.ready ? 1 : 0) << " value " << entry.value << " halt "            << (entry.halt ? 1 : 0) << " result_boundary ";

            if (entry.normal_result_boundary) {

                output << *entry.normal_result_boundary;

            }

            else {

                output << "-";

            }

            output << "\n";

        }

        for (std::size_t kind_index = 0; kind_index < 7; ++kind_index) {

            const UnitEntry* unit = impl_->FindUnit(         static_cast<UnitRole>(kind_index));

            const ExecutionPipeline* pipeline = unit == nullptr         ? nullptr         : dynamic_cast<const ExecutionPipeline*>(unit->unit.get());

            if (pipeline == nullptr) {

                throw std::runtime_error("registered execution pipeline disappeared");

            }

            const ExecutionSnapshot snapshot = pipeline->Snapshot();

            output << "UNIT " << ExecutionUnitKindName(snapshot.kind)            << " next_dispatch "            << snapshot.next_legal_dispatch_boundary << "\n";

            for (const ExecutionStageSnapshot& row : snapshot.stages) {

                output << "  stage " << row.stage << " "              << (row.valid ? "VALID" : "EMPTY");

                if (row.valid) {

                    output << " seq " << row.sequence << " pc 0x" << std::hex                << std::setw(8) << std::setfill('0') << row.pc << std::dec                << std::setfill(' ') << " opcode " << row.opcode                << " dispatch " << row.dispatch_boundary << " result "                << row.nominal_result_boundary << " lhs " << row.lhs                << " rhs " << row.rhs << " store " << row.store_value                << " rd " << row.destination << " write "                << (row.writes_register ? 1 : 0);

                }

                output << "\n";

            }

        }

        for (std::size_t index = 0;        index < impl_->machine->impl_->reservation_current.size(); ++index) {

            const auto& owner = impl_->machine->impl_->reservation_current[index];

            if (owner) {

                output << "RESERVATION boundary "              << (impl_->machine->impl_->current_boundary + index + 1U)              << " sequence " << *owner << "\n";

            }

        }

        output << "END PIPELINE\n";

    }

    void Processor::PrintStructure(std::ostream& output) const {

        output << "BEGIN STRUCTURE\nstatus OK\n";

        for (const NodeRecord& node : impl_->nodes) {

            output << "node " << node.id << " " << node.name << " ";

            if (node.kind == NodeRecord::Kind::kStage) {

                output << "STAGE " << StageRoleName(node.stage_role);

            }

            else if (node.kind == NodeRecord::Kind::kRegister) {

                output << "REGISTER " << RegisterRoleName(node.register_role);

            }

            else if (node.kind == NodeRecord::Kind::kUnit) {

                output << "UNIT " << UnitRoleName(node.unit_role);

            }

            else {

                output << "CACHE " << cache::CacheRoleName(node.cache_role);

            }

            output << "\n";

        }

        for (const EdgeRecord& edge : impl_->edges) {

            output << "edge " << edge.source << " " << edge.destination << " "            << ConnectionTypeName(edge.type) << "\n";

        }

        for (std::size_t role_index = 0; role_index < 11; ++role_index) {

            const UnitEntry* unit =         impl_->FindUnit(static_cast<UnitRole>(role_index));

            if (unit != nullptr) {

                output << "uses " << unit->name << " " << unit->unit->Uses() << "\n";

            }

        }

        output << "END STRUCTURE\n";

    }

    void Processor::PrintStatistics(std::ostream& output) const {

        output << "BEGIN STATISTICS\ncycles " << impl_->cycles << "\nretired "          << impl_->machine->RetiredInstructions() << "\ndata_stalls "          << impl_->machine->DataStalls() << "\ncontrol_stalls "          << impl_->machine->ControlStalls() << "\n";

        impl_->machine->PrintStatistics(output);

        const UnitEntry* unit = impl_->FindUnit(UnitRole::kBranchPrediction);

        const BranchPredictionSystem* branch_prediction =       unit == nullptr           ? nullptr           : dynamic_cast<const BranchPredictionSystem*>(unit->unit.get());

        if (branch_prediction == nullptr) {

            throw std::runtime_error("branch prediction unit is missing");

        }

        branch_prediction->PrintStatistics(output);

        output << "END STATISTICS\n";

    }

}

// namespace pipesim
