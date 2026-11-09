#include "framework/internal.h"
#include "branch_prediction.h"

namespace pipesim {

    using namespace framework_internal;

    Processor::Processor(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {

    }

    Processor::~Processor() = default;

    void Processor::Reset() {

        impl_->machine->Reset();

        impl_->cycles = 0;

        for (UnitEntry& unit : impl_->units) {

            unit.unit->uses_ = 0;

            unit.unit->Reset();

        }

        for (StageEntry& stage : impl_->stages) stage.component->Reset();

        for (RegisterEntry& state : impl_->registers) state.state->Reset();

    }

    void Processor::Clock() {

        if (impl_->machine->IsHalted()) return;

        impl_->machine->BeginCycle(impl_->cycles);

        for (UnitEntry& unit : impl_->units) {

            if (ExecutionPipeline* pipeline =             dynamic_cast<ExecutionPipeline*>(unit.unit.get())) {

                pipeline->BeginCycle();

            }

        }

        const StageRole order[] = {

            StageRole::kWriteback, StageRole::kMemory,                              StageRole::kExecute, StageRole::kDecode,                              StageRole::kFetch
        };

        for (StageRole role : order) impl_->FindStage(role)->component->Evaluate();

        impl_->machine->EndCycle(impl_->cycles);

        for (RegisterEntry& state : impl_->registers) state.state->Commit();

        for (UnitEntry& unit : impl_->units) {

            if (ExecutionPipeline* pipeline =             dynamic_cast<ExecutionPipeline*>(unit.unit.get())) {

                pipeline->Commit();

            }

        }

        impl_->machine->CommitCycle();

        ++impl_->cycles;

    }

    bool Processor::IsHalted() const {

        return impl_->machine->IsHalted();

    }

    std::uint64_t Processor::Cycles() const {

        return impl_->cycles;

    }

    void Processor::SelectPredictor(std::size_t predictor_number) {

        UnitEntry* unit = impl_->FindUnit(UnitRole::kBranchPrediction);

        BranchPredictionSystem* branch_prediction =       unit == nullptr           ? nullptr           : dynamic_cast<BranchPredictionSystem*>(unit->unit.get());

        if (branch_prediction == nullptr) {

            throw std::runtime_error("branch prediction unit is missing");

        }

        branch_prediction->SelectPredictor(predictor_number);

    }

}

// namespace pipesim
