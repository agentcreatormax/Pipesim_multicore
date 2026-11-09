#pragma once

#include "speculation.h"

#include <vector>

namespace pipesim {

class SpeculationController::Impl {
 public:
  explicit Impl(std::size_t maximum) : maximum_checkpoints(maximum) {}
  std::size_t maximum_checkpoints;
  std::vector<BranchCheckpoint> checkpoints;
};

}
