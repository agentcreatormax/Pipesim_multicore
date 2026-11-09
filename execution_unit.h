#ifndef PIPESIM_EXECUTION_UNIT_H_
#define PIPESIM_EXECUTION_UNIT_H_

#include "pipesim.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pipesim {

    struct ExecutionOperation {

        std::uint64_t sequence = 0;

        Instruction instruction;

        std::uint32_t lhs = 0;

        std::uint32_t rhs = 0;

        std::uint32_t store_value = 0;

        std::uint64_t dispatch_boundary = 0;

        std::uint64_t fetch_cycle = 0;

        BranchPredictionSet predictions;

    };

    struct ExecutionResult {

        std::uint64_t sequence = 0;

        Instruction instruction;

        std::uint64_t nominal_result_boundary = 0;

        std::uint32_t value = 0;

        bool memory = false;

        bool memory_write = false;

        std::uint32_t address = 0;

        std::uint32_t store_value = 0;

        bool redirect = false;

        std::uint32_t next_pc = 0;

        bool halt = false;

        std::uint64_t fetch_cycle = 0;

        BranchPredictionSet predictions;

    };

    struct ExecutionStageSnapshot {

        bool valid = false;

        std::uint64_t sequence = 0;

        std::uint32_t pc = 0;

        std::string opcode = "NOP";

        std::string unit;

        std::uint32_t stage = 0;

        std::uint64_t dispatch_boundary = 0;

        std::uint64_t nominal_result_boundary = 0;

        std::uint32_t lhs = 0;

        std::uint32_t rhs = 0;

        std::uint32_t store_value = 0;

        int destination = 0;

        bool writes_register = false;

    };

    struct ExecutionSnapshot {

        ExecutionUnitKind kind = ExecutionUnitKind::kAdd;

        std::uint64_t next_legal_dispatch_boundary = 0;

        std::vector<ExecutionStageSnapshot> stages;

    };

    class ExecutionPipeline : public TrackedUnit {

        public:   ExecutionPipeline(ExecutionUnitKind kind,                     const ExecutionUnitConfig& config);

        ~ExecutionPipeline() override;

        ExecutionPipeline(const ExecutionPipeline&) = delete;

        ExecutionPipeline& operator=(const ExecutionPipeline&) = delete;

        void Reset() override;

        void BeginCycle();

        ExecutionUnitKind kind() const;

        const ExecutionUnitConfig& config() const;

        std::optional<ExecutionResult> OutputIntent() const;

        bool CanAccept(std::uint64_t dispatch_boundary,                  bool output_will_be_consumed) const;

        void Operate(std::uint64_t current_boundary,                const std::optional<ExecutionOperation>& dispatch,                bool consume_output);

        void Commit();

        ExecutionSnapshot Snapshot() const;

        bool HasWork() const;

        std::uint64_t SquashAfter(std::uint64_t sequence);

        private:   class Impl;

        std::unique_ptr<Impl> impl_;

    };

}

// namespace pipesim

#endif  // PIPESIM_EXECUTION_UNIT_H_
