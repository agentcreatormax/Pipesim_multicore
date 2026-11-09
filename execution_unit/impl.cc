#include "execution_unit/internal.h"
#include "execution_unit/helpers.h"
#include <stdexcept>
namespace pipesim {
ExecutionPipeline::Impl::Impl(ExecutionUnitKind unit_kind,const ExecutionUnitConfig& unit_config):kind(unit_kind),config(unit_config){
    if(static_cast<std::size_t>(kind)>static_cast<std::size_t>(ExecutionUnitKind::kAgu))throw std::runtime_error("invalid execution-unit kind");
    if(config.initiation_interval==0)throw std::runtime_error("zero execution initiation interval");
    //start
    const std::size_t stage_count =
        static_cast<std::size_t>(config.latency) + 1;

    current.resize(stage_count);
    next.resize(stage_count);
    //end
}

ExecutionResult ExecutionPipeline::Impl::ResultFor(
    const ExecutionOperation& operation) const {

    ExecutionResult result;

    result.sequence = operation.sequence;
    result.instruction = operation.instruction;

    result.nominal_result_boundary =
        execution_internal::CheckedResultBoundary(
            operation.dispatch_boundary,
            config.latency);
    result.fetch_cycle=operation.fetch_cycle;
    result.predictions=operation.predictions;
    switch (kind) {
        //start
        case ExecutionUnitKind::kAdd:
            result.value = operation.lhs + operation.rhs;
            return result;
        case ExecutionUnitKind::kSub:
            result.value = operation.lhs - operation.rhs;
            return result;
        case ExecutionUnitKind::kAnd:
            result.value = operation.lhs & operation.rhs;
            return result;
        case ExecutionUnitKind::kOr:
            result.value = operation.lhs | operation.rhs;
            return result;
        case ExecutionUnitKind::kXor:
            result.value = operation.lhs ^ operation.rhs;
            return result;
        case ExecutionUnitKind::kControl:
            switch (operation.instruction.opcode) {
                case Opcode::kBeq:
                    result.redirect =
                        operation.lhs == operation.rhs;
                    result.next_pc =
                        result.redirect
                            ? static_cast<std::uint32_t>(
                                  operation.instruction.immediate)
                            : operation.instruction.pc + 4U;
                    return result;
                case Opcode::kBne:
                    result.redirect =
                        operation.lhs != operation.rhs;
                    result.next_pc =
                        result.redirect
                            ? static_cast<std::uint32_t>(
                                  operation.instruction.immediate)
                            : operation.instruction.pc + 4U;
                    return result;
                case Opcode::kJ:
                    result.redirect = true;
                    result.next_pc =
                        static_cast<std::uint32_t>(
                            operation.instruction.immediate);
                    return result;
                case Opcode::kHalt:
                    result.halt = true;
                    result.next_pc =
                        operation.instruction.pc + 4U;
                    return result;
                default:
                    break;
            }
            break;
        case ExecutionUnitKind::kAgu:
            switch (operation.instruction.opcode) {

                case Opcode::kLw:
                    result.memory = true;
                    result.address =
                        operation.lhs + operation.rhs;
                    return result;

                case Opcode::kSw:
                    result.memory_write = true;
                    result.address =
                        operation.lhs + operation.rhs;
                    result.store_value =
                        operation.store_value;
                    return result;
                //Start
                case Opcode::kFence:
                case Opcode::kXchg:
                    result.memory=true;
                    result.address=operation.lhs+operation.rhs;
                    result.store_value=operation.store_value;
                    return result;
                //End
                default:
                    break;
            }
            break;
    }

    throw std::runtime_error(
        "ExecutionPipeline::ResultFor: opcode does not match execution unit");
}
}