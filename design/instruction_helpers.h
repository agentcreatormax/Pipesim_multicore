#pragma once
#include "pipesim.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace pipesim::design_internal {

    bool IsControl(Opcode opcode);

    bool ReadsRs1(Opcode opcode);

    bool ReadsRs2(Opcode opcode);

    bool WritesRegister(const Instruction& instruction);

    ExecutionUnitKind UnitFor(Opcode opcode);

    std::uint64_t ResultBoundary(std::uint64_t dispatch, std::uint32_t latency);

}

