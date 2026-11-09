#include "branch_prediction/internal.h"

#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace pipesim {

    namespace {

        void Increase(std::uint64_t* value) {

            if (*value == std::numeric_limits<std::uint64_t>::max()) {

                throw std::runtime_error("branch metric overflow");

            }

            ++*value;

        }

    }

    BranchPredictionSystem::BranchPredictionSystem(     std::unique_ptr<BranchPredictor> first,     std::unique_ptr<BranchPredictor> second,     std::unique_ptr<BranchPredictor> third)     : impl_(new Impl) {

        impl_->predictors[0] = std::move(first);

        impl_->predictors[1] = std::move(second);

        impl_->predictors[2] = std::move(third);

        for (const auto& predictor : impl_->predictors) {

            if (predictor == nullptr) {

                throw std::runtime_error("branch predictor is null");

            }

        }

    }

    BranchPredictionSystem::~BranchPredictionSystem() = default;

    void BranchPredictionSystem::Reset() {

        impl_->metrics = SpeculationMetrics();

        for (auto& predictor : impl_->predictors) predictor->Reset();

    }

    void BranchPredictionSystem::SelectPredictor(std::size_t predictor_number) {

        if (predictor_number >= impl_->predictors.size()) {

            throw std::runtime_error("predictor number must be 0, 1, or 2");

        }

        impl_->selected_predictor = predictor_number;

    }

    std::size_t BranchPredictionSystem::SelectedPredictor() const {

        return impl_->selected_predictor;

    }

    BranchPredictionSet BranchPredictionSystem::Predict(std::uint32_t pc,                                                     std::uint64_t cycle) {

        (void)cycle;

        Touch();

        BranchPredictionSet result;

        result.selected_predictor = impl_->selected_predictor;

        for (std::size_t index = 0; index < impl_->predictors.size(); ++index) {

            result.predictions[index] = impl_->predictors[index]->Predict(pc);

        }

        const BranchPrediction& selected = result.predictions[result.selected_predictor];

        result.next_pc = selected.branch_found && selected.taken ? selected.target : pc + 4;

        return result;

    }

    bool BranchPredictionSystem::ObserveInstruction(     const Instruction& instruction,     const BranchPredictionSet& predictions) {

        for (std::size_t index = 0; index < impl_->predictors.size(); ++index) {

            impl_->predictors[index]->ObserveInstruction(         instruction, predictions.predictions[index]);

        }

        const bool actual_branch = instruction.opcode == Opcode::kBeq ||                              instruction.opcode == Opcode::kBne ||                              instruction.opcode == Opcode::kJ;

        const BranchPrediction& selected =       predictions.predictions[predictions.selected_predictor];

        const bool false_branch = selected.branch_found && !actual_branch;

        if (false_branch) {

            Increase(&impl_->metrics.false_branch_redirects);

            Increase(&impl_->metrics.redirects);

        }

        return false_branch;

    }

    bool BranchPredictionSystem::Resolve(     const BranchResult& result,     const BranchPredictionSet& predictions) {

        if (result.type == BranchType::kNotBranch) {

            throw std::runtime_error("Resolve received a non-branch instruction");

        }

        Increase(&impl_->metrics.branches_resolved);

        if (result.type == BranchType::kConditional) {

            Increase(&impl_->metrics.conditional_branches);

        }

        else {

            Increase(&impl_->metrics.unconditional_branches);

        }

        Increase(result.taken ? &impl_->metrics.taken_branches                         : &impl_->metrics.not_taken_branches);

        for (std::size_t index = 0; index < impl_->predictors.size(); ++index) {

            impl_->predictors[index]->Update(result, predictions.predictions[index]);

        }

        const BranchPrediction& selected =       predictions.predictions[predictions.selected_predictor];

        const bool predicted_taken = selected.branch_found && selected.taken;

        const bool direction_wrong = predicted_taken != result.taken;

        const bool target_wrong = result.taken && predicted_taken &&                             selected.target != result.target;

        if (direction_wrong) {

            Increase(&impl_->metrics.direction_mispredictions);

            Increase(&impl_->metrics.redirects);

        }

        else if (target_wrong) {

            Increase(&impl_->metrics.target_mispredictions);

            Increase(&impl_->metrics.redirects);

        }

        else {

            Increase(&impl_->metrics.correct_predictions);

        }

        return direction_wrong || target_wrong;

    }

    void BranchPredictionSystem::RecordSpeculativeFetch(std::uint64_t depth) {

        Increase(&impl_->metrics.speculative_instructions_fetched);

        impl_->metrics.maximum_speculation_depth =       std::max(impl_->metrics.maximum_speculation_depth, depth);

    }

    void BranchPredictionSystem::RecordSpeculativeDispatch() {

        Increase(&impl_->metrics.speculative_instructions_dispatched);

    }

    void BranchPredictionSystem::RecordSquash(     std::uint64_t fetches,     std::uint64_t pipeline_operations,     std::uint64_t completions,     std::uint64_t data_requests) {

        impl_->metrics.squashed_fetches += fetches;

        impl_->metrics.squashed_pipeline_operations += pipeline_operations;

        impl_->metrics.squashed_completions += completions;

        impl_->metrics.squashed_data_requests += data_requests;

    }

    void BranchPredictionSystem::RecordRecoveryCycle() {

        Increase(&impl_->metrics.recovery_cycles);

    }

    const SpeculationMetrics& BranchPredictionSystem::Metrics() const {

        return impl_->metrics;

    }

    const BranchPredictor& BranchPredictionSystem::Predictor(     std::size_t predictor_number) const {

        if (predictor_number >= impl_->predictors.size()) {

            throw std::runtime_error("predictor number must be 0, 1, or 2");

        }

        return *impl_->predictors[predictor_number];

    }

    void BranchPredictionSystem::PrintStatistics(std::ostream& output) const {

        const SpeculationMetrics& metrics = impl_->metrics;

        output << "branch.selected_predictor " << impl_->selected_predictor << "\n";

        output << "branch.branches_resolved " << metrics.branches_resolved << "\n";

        output << "branch.conditional_branches " << metrics.conditional_branches << "\n";

        output << "branch.unconditional_branches " << metrics.unconditional_branches << "\n";

        output << "branch.taken_branches " << metrics.taken_branches << "\n";

        output << "branch.not_taken_branches " << metrics.not_taken_branches << "\n";

        output << "branch.correct_predictions " << metrics.correct_predictions << "\n";

        output << "branch.direction_mispredictions " << metrics.direction_mispredictions << "\n";

        output << "branch.target_mispredictions " << metrics.target_mispredictions << "\n";

        output << "branch.false_branch_redirects " << metrics.false_branch_redirects << "\n";

        output << "branch.speculative_instructions_fetched " << metrics.speculative_instructions_fetched << "\n";

        output << "branch.speculative_instructions_dispatched " << metrics.speculative_instructions_dispatched << "\n";

        output << "branch.squashed_fetches " << metrics.squashed_fetches << "\n";

        output << "branch.squashed_pipeline_operations " << metrics.squashed_pipeline_operations << "\n";

        output << "branch.squashed_completions " << metrics.squashed_completions << "\n";

        output << "branch.squashed_data_requests " << metrics.squashed_data_requests << "\n";

        output << "branch.redirects " << metrics.redirects << "\n";

        output << "branch.recovery_cycles " << metrics.recovery_cycles << "\n";

        output << "branch.maximum_speculation_depth " << metrics.maximum_speculation_depth << "\n";

        for (std::size_t index = 0; index < impl_->predictors.size(); ++index) {

            const BranchPredictor& predictor = *impl_->predictors[index];

            const PredictorMetrics& predictor_metrics = predictor.Metrics();

            const std::string prefix = "predictor" + std::to_string(index) + ".";

            output << prefix << "name " << predictor.Name() << "\n";

            output << prefix << "storage_bits " << predictor.StorageBits() << "\n";

            output << prefix << "lookups " << predictor_metrics.lookups << "\n";

            output << prefix << "branches_found " << predictor_metrics.branches_found << "\n";

            output << prefix << "branches_not_found " << predictor_metrics.branches_not_found << "\n";

            output << prefix << "predicted_taken " << predictor_metrics.predicted_taken << "\n";

            output << prefix << "predicted_not_taken " << predictor_metrics.predicted_not_taken << "\n";

            output << prefix << "correct_predictions " << predictor_metrics.correct_predictions << "\n";

            output << prefix << "direction_mispredictions " << predictor_metrics.direction_mispredictions << "\n";

            output << prefix << "target_mispredictions " << predictor_metrics.target_mispredictions << "\n";

            output << prefix << "false_branch_predictions " << predictor_metrics.false_branch_predictions << "\n";

            output << prefix << "missed_branches " << predictor_metrics.missed_branches << "\n";

            output << prefix << "updates " << predictor_metrics.updates << "\n";

        }

    }

    const char* BranchTypeName(BranchType type) {

        switch (type) {

            case BranchType::kNotBranch: return "NOT_BRANCH";

            case BranchType::kConditional: return "CONDITIONAL";

            case BranchType::kUnconditional: return "UNCONDITIONAL";

            case BranchType::kHalt: return "HALT";

        }

        return "UNKNOWN";

    }

}

