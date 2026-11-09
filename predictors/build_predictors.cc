#include "branch_prediction.h"
#include "predictors/predictor_one.h"
#include "predictors/predictor_two.h"
#include "predictors/predictor_three.h"

namespace pipesim {

    std::array<std::unique_ptr<BranchPredictor>, 3> BuildPredictors() {

        std::array<std::unique_ptr<BranchPredictor>, 3> predictors;

        predictors[0] = std::make_unique<PredictorOne>();

        predictors[1] = std::make_unique<PredictorTwo>();

        predictors[2] = std::make_unique<PredictorThree>();

        return predictors;

    }

}

