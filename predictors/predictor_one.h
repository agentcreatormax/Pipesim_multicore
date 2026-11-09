#pragma once

#include "branch_prediction.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
namespace pipesim {

struct PredictorOneComponentMetrics {
  std::uint64_t tage_predictions = 0;
  std::uint64_t tage_correct = 0;
  std::array<std::uint64_t, 9> tage_source_predictions{};  // 0=base, 1..8=T1..T8

  std::uint64_t sc_predictions = 0;
  std::uint64_t sc_correct = 0;
  std::uint64_t sc_overrides = 0;
  std::uint64_t sc_override_correct = 0;

  std::uint64_t loop_selections = 0;
  std::uint64_t loop_correct = 0;

  std::uint64_t btb_reads = 0;
  std::uint64_t btb_writes = 0;
  std::uint64_t tage_reads = 0;
  std::uint64_t tage_writes = 0;
  std::uint64_t sc_reads = 0;
  std::uint64_t sc_writes = 0;
  std::uint64_t loop_reads = 0;
  std::uint64_t loop_writes = 0;
};

class PredictorOne final : public BranchPredictor {
 public:
  std::string Name() const override;
  void Reset() override;
  BranchPrediction Predict(std::uint32_t pc) override;
  void ObserveInstruction(const Instruction& instruction,
                          const BranchPrediction& prediction) override;
  void Update(const BranchResult& result,
              const BranchPrediction& prediction) override;
  std::uint64_t StorageBits() const override;
  const PredictorMetrics& Metrics() const override;
  const PredictorOneComponentMetrics& ComponentMetrics() const;

 private:
  static constexpr std::size_t kBtbEntries = 256;
  static constexpr std::size_t kBaseEntries = 1024;
  static constexpr std::size_t kTageTables = 8;
  static constexpr std::size_t kTageEntries = 256;
  static constexpr std::size_t kScTables = 8;
  static constexpr std::size_t kScEntries = 128;
  static constexpr std::size_t kLocalHistoryEntries = 128;
  static constexpr std::size_t kLoopEntries = 64;
  struct BtbEntry {
    bool valid = false;
    std::uint16_t tag = 0;
    std::uint32_t target = 0;
    bool unconditional = false;
  };

  struct TageEntry {
    bool valid = false;
    std::uint16_t tag = 0;  // logical width: 10 bits
    std::int8_t counter = 0;  // logical signed 3-bit counter [-4, 3]
    std::uint8_t useful = 0;  // logical 2-bit counter [0, 3]
  };

  struct ScEntry {
    bool valid = false;
    std::uint8_t tag = 0;      // logical width: 6 bits
    std::int8_t counter = 0;   // logical signed 6-bit counter [-32, 31]
  };

  struct LoopEntry {
    bool valid = false;
    std::uint16_t tag = 0;  // logical width: 12 bits
    bool direction = false;
    std::uint16_t trip_count = 0;        // logical width: 12 bits
    std::uint16_t current_iteration = 0; // logical width: 12 bits
    std::uint8_t confidence = 0;         // logical 3-bit counter
    std::uint8_t age = 0;                // logical 3-bit replacement state
  };

  PredictorMetrics metrics_{};
  PredictorOneComponentMetrics component_metrics_{};

  std::array<BtbEntry, kBtbEntries> btb_{};
  std::array<std::uint8_t, kBaseEntries> base_{};  // logical 2-bit counters
  std::array<std::array<TageEntry, kTageEntries>, kTageTables> tage_{};

  // 2016-style neural/Multi-GEHL statistical corrector adapted to the
  // assignment's direct-indexed + stored-tag requirement.
  std::array<std::array<ScEntry, kScEntries>, kScTables> sc_{};
  std::array<std::int8_t, 4> sc_chooser_{};  // signed 4-bit counters
  std::uint8_t sc_threshold_ = 12;           // logical 6-bit value
  std::int8_t sc_threshold_adapt_ = 0;       // logical signed 6-bit value

  std::array<LoopEntry, kLoopEntries> loop_{};

  // Non-speculative history policy: these histories change only in Update(),
  // after the real conditional-branch result is known. Predict() stores the
  // history snapshot in BranchPrediction::information so the exact lookup
  // context accompanies the dynamic instruction until resolution.
  std::uint32_t global_history_ = 0;
  std::array<std::uint16_t, kLocalHistoryEntries> local_history_{};
  std::uint8_t backward_history_ = 0;

  // TAGE selection/replacement state.
  std::int8_t use_alt_on_new_alloc_ = -1;  // signed 4-bit counter
  std::uint16_t usefulness_age_counter_ = 0; // logical 10-bit counter

  // PredictorMetrics metrics_;
};

}
