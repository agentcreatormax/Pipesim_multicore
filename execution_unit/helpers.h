#pragma once
#include "execution_unit.h"
#include <cstdint>
namespace pipesim::execution_internal {

    bool OpcodeBelongsTo(Opcode,ExecutionUnitKind);

    bool WritesRegister(Opcode,int);

    std::uint64_t CheckedResultBoundary(std::uint64_t,std::uint32_t);

}

