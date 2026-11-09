#pragma once

#include "branch_prediction.h"

namespace pipesim {

    // Fixed shadow-slot filler (predictors/predictor_two.cc). Not a graded
    // predictor; do not modify. See that file for why this slot exists.
    class PredictorTwo final : public BranchPredictor {

        public:   std::string Name() const override;

        void Reset() override;

        BranchPrediction Predict(std::uint32_t pc) override;

        void ObserveInstruction(const Instruction& instruction,                           const BranchPrediction& prediction) override;

        void Update(const BranchResult& result,               const BranchPrediction& prediction) override;

        std::uint64_t StorageBits() const override;

        const PredictorMetrics& Metrics() const override;

        private:   PredictorMetrics metrics_;

    };

}

