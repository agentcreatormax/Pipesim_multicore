#include "design/instruction_helpers.h"
namespace pipesim::design_internal {

    bool IsControl(Opcode opcode) {

        return opcode == Opcode::kBeq || opcode == Opcode::kBne || opcode == Opcode::kJ || opcode == Opcode::kHalt;

    }

    bool ReadsRs1(Opcode opcode) {

        return opcode == Opcode::kAdd || opcode == Opcode::kSub || opcode == Opcode::kAnd || opcode == Opcode::kOr || opcode == Opcode::kXor || opcode == Opcode::kAddi || opcode == Opcode::kLw || opcode == Opcode::kSw || opcode == Opcode::kXchg || opcode == Opcode::kBeq || opcode == Opcode::kBne;

    }

    bool ReadsRs2(Opcode opcode) {

        return opcode == Opcode::kAdd || opcode == Opcode::kSub || opcode == Opcode::kAnd || opcode == Opcode::kOr || opcode == Opcode::kXor || opcode == Opcode::kSw || opcode == Opcode::kXchg || opcode == Opcode::kBeq || opcode == Opcode::kBne;

    }

    bool WritesRegister(const Instruction& instruction) {

        if (instruction.rd == 0) return false;

        const Opcode opcode = instruction.opcode;

        return opcode == Opcode::kAdd || opcode == Opcode::kAddi || opcode == Opcode::kSub || opcode == Opcode::kAnd || opcode == Opcode::kOr || opcode == Opcode::kXor || opcode == Opcode::kLw || opcode == Opcode::kXchg;

    }

    ExecutionUnitKind UnitFor(Opcode opcode) {

        switch (opcode) {

            case Opcode::kAdd: case Opcode::kAddi: return ExecutionUnitKind::kAdd;

            case Opcode::kSub:return ExecutionUnitKind::kSub;

            case Opcode::kAnd:return ExecutionUnitKind::kAnd;

            case Opcode::kOr:return ExecutionUnitKind::kOr;

            case Opcode::kXor:return ExecutionUnitKind::kXor;

            case Opcode::kBeq: case Opcode::kBne: case Opcode::kJ: case Opcode::kHalt:return ExecutionUnitKind::kControl;

            case Opcode::kLw: case Opcode::kSw: case Opcode::kFence: case Opcode::kXchg:return ExecutionUnitKind::kAgu;

            case Opcode::kNop: break;

        }

        throw std::runtime_error("reserved opcode has no execution unit");

    }

    [[maybe_unused]] std::uint64_t ResultBoundary(std::uint64_t dispatch,std::uint32_t latency) {

        const auto delta=static_cast<std::uint64_t>(latency)+1U;

        if(dispatch>std::numeric_limits<std::uint64_t>::max()-delta)throw std::runtime_error("execution result boundary overflow");

        return dispatch+delta;

    }

}

