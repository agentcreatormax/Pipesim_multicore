#ifndef PIPESIM_EXECUTION_CONFIG_H_
#define PIPESIM_EXECUTION_CONFIG_H_

#include <cstdint>

namespace pipesim {

    enum class ExecutionUnitKind {

        kAdd,   kSub,   kAnd,   kOr,   kXor,   kControl,   kAgu,
    };

    struct ExecutionUnitConfig {

        std::uint32_t latency = 0;

        std::uint32_t initiation_interval = 1;

    };

    const char* ExecutionUnitKindName(ExecutionUnitKind kind);

}

// namespace pipesim

#endif  // PIPESIM_EXECUTION_CONFIG_H_
