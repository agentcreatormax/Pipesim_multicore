#include "execution_unit/internal.h"
#include "execution_unit/helpers.h"

#include <stdexcept>
#include <limits>
namespace pipesim {
const char* ExecutionUnitKindName(ExecutionUnitKind kind){switch(kind){case ExecutionUnitKind::kAdd:return "ADD";case ExecutionUnitKind::kSub:return "SUB";case ExecutionUnitKind::kAnd:return "AND";case ExecutionUnitKind::kOr:return "OR";case ExecutionUnitKind::kXor:return "XOR";case ExecutionUnitKind::kControl:return "CONTROL";case ExecutionUnitKind::kAgu:return "AGU";}return "UNKNOWN";}
ExecutionPipeline::ExecutionPipeline(ExecutionUnitKind kind,const ExecutionUnitConfig& config):impl_(new Impl(kind,config)){}
ExecutionPipeline::~ExecutionPipeline()=default;
void ExecutionPipeline::Reset(){
    //start
    impl_->current.assign(
        impl_->current.size(),
        std::nullopt);

    impl_->next.assign(
        impl_->next.size(),
        std::nullopt);

    impl_->next_legal_current = 0;
    impl_->next_legal_next = 0;

    impl_->begun = false;
    impl_->operated = false;
    //end
}

void ExecutionPipeline::BeginCycle(){
    //start
    if (impl_->begun) {
        throw std::runtime_error(
            "execution pipeline BeginCycle called twice");
    }

    impl_->next = impl_->current;
    impl_->next_legal_next =
        impl_->next_legal_current;

    impl_->begun = true;
    impl_->operated = false;
    //end
}

ExecutionUnitKind ExecutionPipeline::kind()const{return impl_->kind;}
const ExecutionUnitConfig& ExecutionPipeline::config()const{return impl_->config;}

std::optional<ExecutionResult> ExecutionPipeline::OutputIntent()const{
    //start
    if (!impl_->current.back().has_value()) {
        return std::nullopt;
    }

    return impl_->ResultFor(*impl_->current.back());
    //end
}

bool ExecutionPipeline::CanAccept(std::uint64_t dispatch_boundary,bool output_will_be_consumed)const{
    (void)dispatch_boundary;
    (void)output_will_be_consumed;
    //start

    // Initiation interval check
    if (dispatch_boundary < impl_->next_legal_current) {
        return false;
    }

    // checking stages status
    std::vector<bool> occupied(impl_->current.size());
    for (std::size_t i = 0; i < impl_->current.size(); ++i) {
        occupied[i] = impl_->current[i].has_value();
    }

    if (output_will_be_consumed) {
        occupied.back() = false;
    }

    //cycle
    for (std::size_t i = occupied.size() - 1; i > 0; --i) {
        if (!occupied[i] && impl_->current[i - 1].has_value()) {
            occupied[i] = true;
            occupied[i - 1] = false;
        }
    }

    // if stage 0 is empty, accept it.
    return !occupied[0];

    //end
}

void ExecutionPipeline::Operate(std::uint64_t current_boundary,const std::optional<ExecutionOperation>& dispatch,bool consume_output){
    if(!impl_->begun||impl_->operated){
        throw std::runtime_error("execution pipeline Operate lifecycle violation");
    }
    (void)current_boundary;
    (void)dispatch;
    (void)consume_output;
    //start

    // if the last stage is consumable, consume it.
    if (consume_output) {
        if (!impl_->next.back().has_value()) {
            throw std::runtime_error("execution pipeline output consume with no output");
        }
        impl_->next.back() = std::nullopt;
    }

    // move the operations in the pipeline forward by one stage.
    for (std::size_t i = impl_->current.size() - 1; i > 0; --i) {
        if (!impl_->next[i].has_value() && impl_->current[i - 1].has_value()) {
            impl_->next[i] = impl_->current[i - 1];
            impl_->next[i - 1] = std::nullopt;
        }
    }

    // welcome new instructions if any.
    if (dispatch.has_value()) {

        if (dispatch->dispatch_boundary != current_boundary + 1) {
            throw std::runtime_error("execution pipeline dispatch boundary mismatch");
        }

        if (!execution_internal::OpcodeBelongsTo(dispatch->instruction.opcode, impl_->kind)) {
            throw std::runtime_error("execution pipeline dispatch opcode routed to wrong unit");
        }

        if (!CanAccept(dispatch->dispatch_boundary, consume_output)) {
            throw std::runtime_error("execution pipeline dispatch is not acceptable");
        }

        if (impl_->next[0].has_value()) {
            throw std::runtime_error("execution pipeline stage 0 blocked for dispatch");
        }

        impl_->next[0] = *dispatch;

        const std::uint64_t ii = static_cast<std::uint64_t>(impl_->config.initiation_interval);

        if (dispatch->dispatch_boundary > std::numeric_limits<std::uint64_t>::max() - ii) {
            throw std::runtime_error("execution initiation boundary overflow");
        }

        impl_->next_legal_next = dispatch->dispatch_boundary + ii;
    }

    impl_->operated = true;

    //end
}

void ExecutionPipeline::Commit(){
    //start

    if (!impl_->begun || !impl_->operated) {
        throw std::runtime_error("execution pipeline Commit lifecycle violation");
    }

    // preparations for the next cycle
    impl_->current = impl_->next;
    impl_->next_legal_current = impl_->next_legal_next;

    impl_->begun = false;
    impl_->operated = false;

    //end
}

ExecutionSnapshot ExecutionPipeline::Snapshot()const{
    //start
    ExecutionSnapshot snapshot;

    snapshot.kind = impl_->kind;
    snapshot.next_legal_dispatch_boundary =
        impl_->next_legal_current;

    snapshot.stages.resize(impl_->current.size());

    for (std::size_t i = 0; i < impl_->current.size(); ++i) {
        ExecutionStageSnapshot& stage = snapshot.stages[i];

        stage.unit = ExecutionUnitKindName(impl_->kind);
        stage.stage = static_cast<std::uint32_t>(i);

        if (!impl_->current[i].has_value()) {
            continue;
        }

        const ExecutionOperation& op = *impl_->current[i];

        stage.valid = true;
        stage.sequence = op.sequence;
        stage.pc = op.instruction.pc;
        stage.opcode = OpcodeName(op.instruction.opcode);
        stage.dispatch_boundary = op.dispatch_boundary;

        stage.nominal_result_boundary = execution_internal::CheckedResultBoundary(op.dispatch_boundary, impl_->config.latency);

        stage.lhs = op.lhs;
        stage.rhs = op.rhs;
        stage.store_value = op.store_value;
        stage.destination = op.instruction.rd;

        stage.writes_register = execution_internal::WritesRegister(op.instruction.opcode, op.instruction.rd);
    }

    return snapshot;
    //end
}

bool ExecutionPipeline::HasWork()const{
    //start
    for (const auto& stage : impl_->current) {
        if (stage.has_value()) {
            return true;
        }
    }
    return false;
   //end
}
std::uint64_t ExecutionPipeline::SquashAfter(std::uint64_t sequence){
    std::uint64_t removed = 0;
    for (auto& operation : impl_->next) {
        if (operation.has_value() && operation->sequence > sequence) {
            operation.reset();
            ++removed;
        }
    }
    return removed;
}

}