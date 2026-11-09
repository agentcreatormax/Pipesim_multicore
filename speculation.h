#ifndef PIPESIM_SPECULATION_H_
#define PIPESIM_SPECULATION_H_

#include "branch_prediction.h"

#include <cstdint>
#include <memory>

namespace pipesim {

    struct BranchCheckpoint {

        std::uint64_t sequence = 0;

        std::uint32_t predicted_next_pc = 0;

        BranchPredictionSet predictions;

    };

    struct RecoveryDecision {

        bool mispredicted = false;

        std::uint64_t branch_sequence = 0;

        std::uint32_t correct_next_pc = 0;

    };

    class SpeculationController final : public TrackedUnit {

        public:   explicit SpeculationController(std::size_t maximum_checkpoints);

        ~SpeculationController() override;

        void Reset() override;

        bool CanFetch() const;

        bool CanDispatch(std::uint64_t sequence,                    const Instruction& instruction) const;

        void AddCheckpoint(const BranchCheckpoint& checkpoint);

        bool IsSpeculative(std::uint64_t sequence) const;

        std::uint64_t SpeculationDepth(std::uint64_t sequence) const;

        RecoveryDecision Resolve(const BranchResult& result);

        void RemoveYoungerCheckpoints(std::uint64_t sequence);

        std::size_t CheckpointCount() const;

        private:   class Impl;

        std::unique_ptr<Impl> impl_;

    };

}

#endif
