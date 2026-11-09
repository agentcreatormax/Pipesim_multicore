#include "predictors/predictor_two.h"

namespace pipesim {

    // Fixed shadow-slot filler. Not a graded predictor and not to be modified.
    //
    // This slot exists only because BranchPredictionSystem calls Predict/Update
    // on all three slots every branch (see README, "shadow prediction"), and
    // the course predictor for PA3 occupies slot 0 alone. This implementation
    // always claims no branch is found and treats every outcome as an implicit
    // not-taken prediction, so it needs no storage and no history state.

    std::string PredictorTwo::Name() const {

        return "UNUSED_SHADOW_SLOT";

    }

    void PredictorTwo::Reset() {

        metrics_ = PredictorMetrics();

    }

    BranchPrediction PredictorTwo::Predict(std::uint32_t pc) {

        (void)pc;

        ++metrics_.lookups;

        ++metrics_.branches_not_found;

        ++metrics_.predicted_not_taken;

        BranchPrediction prediction;

        prediction.branch_found = false;

        prediction.taken = false;

        prediction.target = 0;

        prediction.confidence = 0;

        prediction.information = 0;

        return prediction;

    }

    void PredictorTwo::ObserveInstruction(     const Instruction& instruction,     const BranchPrediction& prediction) {

        (void)prediction;

        if (instruction.opcode == Opcode::kBeq ||       instruction.opcode == Opcode::kBne ||       instruction.opcode == Opcode::kJ) {

            ++metrics_.missed_branches;

        }

    }

    void PredictorTwo::Update(const BranchResult& result,                           const BranchPrediction& prediction) {

        (void)prediction;

        ++metrics_.updates;

        if (result.taken) {

            ++metrics_.direction_mispredictions;

        }

        else {

            ++metrics_.correct_predictions;

        }

    }

    std::uint64_t PredictorTwo::StorageBits() const {

        return 0;

    }

    const PredictorMetrics& PredictorTwo::Metrics() const {

        return metrics_;

    }

}

