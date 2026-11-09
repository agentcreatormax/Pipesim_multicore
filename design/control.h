#pragma once
#include "pipesim.h"
#include "design/instruction_helpers.h"
namespace pipesim::design_internal {
struct DecodeResult { ExecutionUnitKind unit=ExecutionUnitKind::kAdd; bool writes_register=false; };
class MainControl final : public TrackedUnit { public: DecodeResult Decode(const Instruction& instruction); };
enum class HazardResult { kNone, kRaw, kWaw };
class HazardControl final : public TrackedUnit { public: HazardResult Check(Machine* machine, std::uint64_t sequence, const Instruction& instruction, bool writes_register); };
}
