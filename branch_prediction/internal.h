#pragma once

#include "branch_prediction.h"

#include <array>
#include <memory>

namespace pipesim {

    class BranchPredictionSystem::Impl {

        public:   std::array<std::unique_ptr<BranchPredictor>, 3> predictors;

        std::size_t selected_predictor = 0;

        SpeculationMetrics metrics;

    };

}

