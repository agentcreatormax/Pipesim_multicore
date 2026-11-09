#pragma once
#include "execution_unit.h"
#include <cstdint>
#include <optional>
#include <vector>
namespace pipesim {
class ExecutionPipeline::Impl {
 public:
  Impl(ExecutionUnitKind, const ExecutionUnitConfig&);
  ExecutionResult ResultFor(const ExecutionOperation&) const;
  ExecutionUnitKind kind; ExecutionUnitConfig config;
  std::vector<std::optional<ExecutionOperation>> current,next;
  std::uint64_t next_legal_current=0,next_legal_next=0;
  bool begun=false,operated=false;
};
}
