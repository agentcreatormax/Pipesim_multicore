#include "execution_unit/internal.h"
#include <limits>
#include <stdexcept>
namespace pipesim::execution_internal {

    bool OpcodeBelongsTo(Opcode opcode,ExecutionUnitKind kind) {

        switch(kind) {

            case ExecutionUnitKind::kAdd:return opcode==Opcode::kAdd||opcode==Opcode::kAddi;

            case ExecutionUnitKind::kSub:return opcode==Opcode::kSub;

            case ExecutionUnitKind::kAnd:return opcode==Opcode::kAnd;

            case ExecutionUnitKind::kOr:return opcode==Opcode::kOr;

            case ExecutionUnitKind::kXor:return opcode==Opcode::kXor;

            case ExecutionUnitKind::kControl:return opcode==Opcode::kBeq||opcode==Opcode::kBne||opcode==Opcode::kJ||opcode==Opcode::kHalt;

            case ExecutionUnitKind::kAgu:return opcode==Opcode::kLw||opcode==Opcode::kSw||opcode==Opcode::kFence||opcode==Opcode::kXchg;

        }

        return false;

    }

    bool WritesRegister(Opcode opcode,int destination) {

        if(destination==0)return false;

        return opcode==Opcode::kAdd||opcode==Opcode::kAddi||opcode==Opcode::kSub||opcode==Opcode::kAnd||opcode==Opcode::kOr||opcode==Opcode::kXor||opcode==Opcode::kLw||opcode==Opcode::kXchg;

    }

    std::uint64_t CheckedResultBoundary(std::uint64_t dispatch,std::uint32_t latency) {

        const auto delta=static_cast<std::uint64_t>(latency)+1U;

        if(dispatch>std::numeric_limits<std::uint64_t>::max()-delta)throw std::runtime_error("execution result boundary overflow");

        return dispatch+delta;

    }

}

