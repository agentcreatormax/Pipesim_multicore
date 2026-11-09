#ifndef PIPESIM_BRANCH_PREDICTION_H_
#define PIPESIM_BRANCH_PREDICTION_H_

#include "pipesim.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>

namespace pipesim {

    struct BranchResult {

        std::uint64_t sequence = 0;

        std::uint64_t fetch_cycle = 0;

        std::uint64_t resolve_cycle = 0;

        std::uint32_t pc = 0;

        BranchType type = BranchType::kNotBranch;

        bool taken = false;

        std::uint32_t target = 0;

    };

    struct PredictorMetrics {

        std::uint64_t lookups = 0;

        std::uint64_t branches_found = 0;

        std::uint64_t branches_not_found = 0;

        std::uint64_t predicted_taken = 0;

        std::uint64_t predicted_not_taken = 0;

        std::uint64_t correct_predictions = 0;

        std::uint64_t direction_mispredictions = 0;

        std::uint64_t target_mispredictions = 0;

        std::uint64_t false_branch_predictions = 0;

        std::uint64_t missed_branches = 0;

        std::uint64_t updates = 0;

    };

    struct SpeculationMetrics {

        std::uint64_t branches_resolved = 0;

        std::uint64_t conditional_branches = 0;

        std::uint64_t unconditional_branches = 0;

        std::uint64_t taken_branches = 0;

        std::uint64_t not_taken_branches = 0;

        std::uint64_t correct_predictions = 0;

        std::uint64_t direction_mispredictions = 0;

        std::uint64_t target_mispredictions = 0;

        std::uint64_t false_branch_redirects = 0;

        std::uint64_t speculative_instructions_fetched = 0;

        std::uint64_t speculative_instructions_dispatched = 0;

        std::uint64_t squashed_fetches = 0;

        std::uint64_t squashed_pipeline_operations = 0;

        std::uint64_t squashed_completions = 0;

        std::uint64_t squashed_data_requests = 0;

        std::uint64_t redirects = 0;

        std::uint64_t recovery_cycles = 0;

        std::uint64_t maximum_speculation_depth = 0;

    };

    class BranchPredictor {

        public:   virtual ~BranchPredictor() = default;

        virtual std::string Name() const = 0;

        virtual void Reset() = 0;

        virtual BranchPrediction Predict(std::uint32_t pc) = 0;

        virtual void ObserveInstruction(const Instruction& instruction,                                   const BranchPrediction& prediction) = 0;

        virtual void Update(const BranchResult& result,                       const BranchPrediction& prediction) = 0;

        virtual std::uint64_t StorageBits() const = 0;

        virtual const PredictorMetrics& Metrics() const = 0;

    };

    class BranchPredictionSystem final : public TrackedUnit {

        public:   BranchPredictionSystem(std::unique_ptr<BranchPredictor> first,                          std::unique_ptr<BranchPredictor> second,                          std::unique_ptr<BranchPredictor> third);

        ~BranchPredictionSystem() override;

        void Reset() override;

        void SelectPredictor(std::size_t predictor_number);

        std::size_t SelectedPredictor() const;

        BranchPredictionSet Predict(std::uint32_t pc, std::uint64_t cycle);

        bool ObserveInstruction(const Instruction& instruction,                           const BranchPredictionSet& predictions);

        bool Resolve(const BranchResult& result,                const BranchPredictionSet& predictions);

        void RecordSpeculativeFetch(std::uint64_t depth);

        void RecordSpeculativeDispatch();

        void RecordSquash(std::uint64_t fetches,                     std::uint64_t pipeline_operations,                     std::uint64_t completions,                     std::uint64_t data_requests);

        void RecordRecoveryCycle();

        const SpeculationMetrics& Metrics() const;

        const BranchPredictor& Predictor(std::size_t predictor_number) const;

        void PrintStatistics(std::ostream& output) const;

        private:   class Impl;

        std::unique_ptr<Impl> impl_;

    };

    std::array<std::unique_ptr<BranchPredictor>, 3> BuildPredictors();

    const char* BranchTypeName(BranchType type);

}

#endif
