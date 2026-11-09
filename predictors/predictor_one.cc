#include "predictors/predictor_one.h"

#include <stdexcept>
#include <array>
#include <cstdint>
#include <string>

namespace pipesim
{
  namespace
  {

    constexpr std::array<unsigned, 8> kTageHistoryLengths = {
        2, 4, 6, 9, 13, 19, 27, 32};
    constexpr std::array<unsigned, 4> kScGlobalHistoryLengths = {
        4, 8, 16, 32};
    constexpr std::array<unsigned, 2> kScLocalHistoryLengths = {
        4, 16};

    constexpr std::int8_t kTageCounterMin = -4;
    constexpr std::int8_t kTageCounterMax = 3;
    constexpr std::int8_t kScCounterMin = -32;
    constexpr std::int8_t kScCounterMax = 31;
    constexpr std::int8_t kChooserMin = -8;
    constexpr std::int8_t kChooserMax = 7;
    constexpr std::uint16_t kLoopCounterMax = 0x0fff;

    std::uint32_t Mix32(std::uint32_t value)
    {
      value ^= value >> 16;
      value *= 0x7feb352dU;
      value ^= value >> 15;
      value *= 0x846ca68bU;
      value ^= value >> 16;
      return value;
    }

    std::uint32_t HistoryMask(unsigned length)
    {
      if (length >= 32)
        return 0xffffffffU;
      return (1U << length) - 1U;
    }

    std::uint32_t FoldHistory(std::uint32_t history, unsigned length)
    {
      const std::uint32_t selected = history & HistoryMask(length);
      return selected ^ (selected >> 7) ^ (selected >> 13) ^ (selected >> 21);
    }

    std::uint16_t BtbTag(std::uint32_t pc)
    {
      return static_cast<std::uint16_t>(((pc >> 10) ^ (pc >> 26)) & 0xffffU);
    }

    std::size_t BtbIndex(std::uint32_t pc)
    {
      return static_cast<std::size_t>((pc >> 2) & 0xffU);
    }

    std::size_t BaseIndex(std::uint32_t pc)
    {
      return static_cast<std::size_t>((pc >> 2) & 0x3ffU);
    }

    std::size_t LocalHistoryIndex(std::uint32_t pc)
    {
      return static_cast<std::size_t>(Mix32(pc >> 2) & 0x7fU);
    }

    std::size_t TageIndex(std::uint32_t pc,
                          std::uint32_t global_history,
                          std::size_t table)
    {
      const unsigned length = kTageHistoryLengths[table];
      const std::uint32_t folded = FoldHistory(global_history, length);
      const std::uint32_t mixed = Mix32((pc >> 2) ^ folded ^
                                        static_cast<std::uint32_t>(
                                            0x9e3779b9U * (table + 1)));
      return static_cast<std::size_t>(mixed & 0xffU);
    }

    std::uint16_t TageTag(std::uint32_t pc,
                          std::uint32_t global_history,
                          std::size_t table)
    {
      const unsigned length = kTageHistoryLengths[table];
      const std::uint32_t history = global_history & HistoryMask(length);
      const std::uint32_t folded = history ^ (history >> 5) ^
                                   (history >> 11) ^ (history >> 19);
      const std::uint32_t mixed = Mix32((pc >> 2) ^ folded ^
                                        static_cast<std::uint32_t>(
                                            0x45d9f3bU * (table + 3)));
      return static_cast<std::uint16_t>(mixed & 0x03ffU);
    }

    std::size_t LoopIndex(std::uint32_t pc)
    {
      return static_cast<std::size_t>(Mix32((pc >> 2) ^ 0x51ed270bU) & 0x3fU);
    }

    std::uint16_t LoopTag(std::uint32_t pc)
    {
      return static_cast<std::uint16_t>(Mix32((pc >> 2) ^ 0xa5a5f00dU) &
                                        0x0fffU);
    }

    void UpdateBaseCounter(std::uint8_t *counter, bool taken)
    {
      if (taken)
      {
        if (*counter < 3)
          ++*counter;
      }
      else
      {
        if (*counter > 0)
          --*counter;
      }
    }

    void UpdateSignedCounter(std::int8_t *counter,
                             bool taken,
                             std::int8_t minimum,
                             std::int8_t maximum)
    {
      if (taken)
      {
        if (*counter < maximum)
          ++*counter;
      }
      else
      {
        if (*counter > minimum)
          --*counter;
      }
    }

    void UpdateChooser(std::int8_t *counter, bool increase)
    {
      if (increase)
      {
        if (*counter < kChooserMax)
          ++*counter;
      }
      else
      {
        if (*counter > kChooserMin)
          --*counter;
      }
    }

    std::uint32_t CounterConfidence(std::int8_t counter)
    {
      if (counter <= -4 || counter >= 3)
        return 3;
      if (counter <= -3 || counter >= 2)
        return 2;
      return 1;
    }

    std::uint32_t BaseConfidence(std::uint8_t counter)
    {
      return (counter == 0 || counter == 3) ? 2U : 1U;
    }

    bool IsConditionalOpcode(Opcode opcode)
    {
      return opcode == Opcode::kBeq || opcode == Opcode::kBne;
    }

    bool IsBranchOpcode(Opcode opcode)
    {
      return IsConditionalOpcode(opcode) || opcode == Opcode::kJ;
    }

    // BranchPrediction::information layout:
    //   [31:0]   global history snapshot
    //   [47:32]  local history snapshot for this PC
    //   [55:48]  backward-branch history snapshot
    //   [59:56]  TAGE provider code (0=base, 1..8=T1..T8)
    //   [63:60]  TAGE alternate code (0=base, 1..8=T1..T8)
    std::uint64_t PackInformation(std::uint32_t global_history,
                                  std::uint16_t local_history,
                                  std::uint8_t backward_history,
                                  std::uint32_t provider_code,
                                  std::uint32_t alternate_code)
    {
      return static_cast<std::uint64_t>(global_history) |
             (static_cast<std::uint64_t>(local_history) << 32) |
             (static_cast<std::uint64_t>(backward_history) << 48) |
             (static_cast<std::uint64_t>(provider_code & 0xfU) << 56) |
             (static_cast<std::uint64_t>(alternate_code & 0xfU) << 60);
    }

    std::uint32_t SavedGlobalHistory(std::uint64_t information)
    {
      return static_cast<std::uint32_t>(information & 0xffffffffULL);
    }

    std::uint16_t SavedLocalHistory(std::uint64_t information)
    {
      return static_cast<std::uint16_t>((information >> 32) & 0xffffULL);
    }

    std::uint8_t SavedBackwardHistory(std::uint64_t information)
    {
      return static_cast<std::uint8_t>((information >> 48) & 0xffULL);
    }

    std::uint32_t SavedProviderCode(std::uint64_t information)
    {
      return static_cast<std::uint32_t>((information >> 56) & 0xfULL);
    }

    std::uint32_t SavedAlternateCode(std::uint64_t information)
    {
      return static_cast<std::uint32_t>((information >> 60) & 0xfULL);
    }

    // BranchPrediction::confidence also carries predictor-private metadata. The
    // framework treats confidence as opaque, so this lets Update() train the exact
    // decision that was made at fetch time without retaining hidden per-branch
    // state that would be difficult to release on a squash.
    //   bits  0..1  final confidence
    //   bits  2..3  TAGE confidence
    //   bit      4  TAGE output
    //   bit      5  SC output
    //   bit      6  loop prediction valid/selected
    //   bit      7  loop output
    //   bit      8  SC overrode TAGE
    //   bit      9  loop supplied final conditional prediction
    //   bit     10  alternate prediction
    //   bit     11  raw provider prediction
    //   bits 12..19 SC absolute score (clamped to 255)
    //   bit     20  BTB said unconditional
    //   bits 21..24 actual TAGE source (0=base, 1..8=T1..T8)
    std::uint32_t PackConfidence(std::uint32_t final_confidence,
                                 std::uint32_t tage_confidence,
                                 bool tage_prediction,
                                 bool sc_prediction,
                                 bool loop_valid,
                                 bool loop_prediction,
                                 bool used_sc,
                                 bool used_loop,
                                 bool alternate_prediction,
                                 bool provider_prediction,
                                 std::uint32_t sc_strength,
                                 bool btb_unconditional,
                                 std::uint32_t tage_source)
    {
      return (final_confidence & 0x3U) |
             ((tage_confidence & 0x3U) << 2) |
             (static_cast<std::uint32_t>(tage_prediction) << 4) |
             (static_cast<std::uint32_t>(sc_prediction) << 5) |
             (static_cast<std::uint32_t>(loop_valid) << 6) |
             (static_cast<std::uint32_t>(loop_prediction) << 7) |
             (static_cast<std::uint32_t>(used_sc) << 8) |
             (static_cast<std::uint32_t>(used_loop) << 9) |
             (static_cast<std::uint32_t>(alternate_prediction) << 10) |
             (static_cast<std::uint32_t>(provider_prediction) << 11) |
             ((sc_strength & 0xffU) << 12) |
             (static_cast<std::uint32_t>(btb_unconditional) << 20) |
             ((tage_source & 0xfU) << 21);
    }

    std::uint32_t SavedTageConfidence(std::uint32_t packed)
    {
      return (packed >> 2) & 0x3U;
    }

    bool SavedTagePrediction(std::uint32_t packed)
    {
      return ((packed >> 4) & 1U) != 0;
    }

    bool SavedScPrediction(std::uint32_t packed)
    {
      return ((packed >> 5) & 1U) != 0;
    }

    bool SavedLoopValid(std::uint32_t packed)
    {
      return ((packed >> 6) & 1U) != 0;
    }

    bool SavedLoopPrediction(std::uint32_t packed)
    {
      return ((packed >> 7) & 1U) != 0;
    }

    bool SavedUsedSc(std::uint32_t packed)
    {
      return ((packed >> 8) & 1U) != 0;
    }

    bool SavedUsedLoop(std::uint32_t packed)
    {
      return ((packed >> 9) & 1U) != 0;
    }

    bool SavedAlternatePrediction(std::uint32_t packed)
    {
      return ((packed >> 10) & 1U) != 0;
    }

    bool SavedProviderPrediction(std::uint32_t packed)
    {
      return ((packed >> 11) & 1U) != 0;
    }

    std::uint32_t SavedScStrength(std::uint32_t packed)
    {
      return (packed >> 12) & 0xffU;
    }

    std::uint32_t SavedTageSource(std::uint32_t packed)
    {
      return (packed >> 21) & 0xfU;
    }

    std::array<std::uint32_t, 8> ScContexts(std::uint32_t pc,
                                           std::uint32_t global_history,
                                           std::uint16_t local_history,
                                           std::uint8_t backward_history,
                                           bool tage_prediction,
                                           std::uint32_t tage_confidence)
    {
      std::array<std::uint32_t, 8> context{};
      const std::uint32_t pc_word = pc >> 2;

      context[0] = pc_word ^
                   (tage_prediction ? 0x13579bdfU : 0x2468ace0U) ^
                   (tage_confidence * 0x9e3779b9U);

      for (std::size_t i = 0; i < 4; ++i)
      {
        const std::uint32_t folded =
            FoldHistory(global_history, kScGlobalHistoryLengths[i]);
        context[1 + i] = pc_word ^ folded ^
                         static_cast<std::uint32_t>(0x7f4a7c15U * (i + 1));
      }

      for (std::size_t i = 0; i < 2; ++i)
      {
        const unsigned length = kScLocalHistoryLengths[i];
        const std::uint32_t mask = (1U << length) - 1U;
        const std::uint32_t folded =
            static_cast<std::uint32_t>(local_history) & mask;
        context[5 + i] = pc_word ^ folded ^
                         static_cast<std::uint32_t>(0xc2b2ae35U * (i + 1));
      }

      context[7] = pc_word ^ static_cast<std::uint32_t>(backward_history) ^
                   0x27d4eb2fU;
      return context;
    }

    std::array<std::size_t, 8> ScIndices(
        const std::array<std::uint32_t, 8>& context)
    {
      std::array<std::size_t, 8> result{};
      for (std::size_t table = 0; table < result.size(); ++table)
      {
        result[table] = static_cast<std::size_t>(
            Mix32(context[table] ^
                  static_cast<std::uint32_t>(0x9e3779b9U * (table + 11))) &
            0x7fU);
      }
      return result;
    }

    std::array<std::uint8_t, 8> ScTags(
        const std::array<std::uint32_t, 8>& context)
    {
      std::array<std::uint8_t, 8> result{};
      for (std::size_t table = 0; table < result.size(); ++table)
      {
        result[table] = static_cast<std::uint8_t>(
            Mix32(context[table] ^
                  static_cast<std::uint32_t>(0x85ebca6bU * (table + 3))) &
            0x3fU);
      }
      return result;
    }

  } // namespace

  std::string PredictorOne::Name() const
  {
    return "TAGE_SC_L_2016_8KB_DIRECT";
  }

  void PredictorOne::Reset()
  {
    metrics_ = PredictorMetrics();
    component_metrics_ = PredictorOneComponentMetrics();

    for (auto &entry : btb_)
      entry = BtbEntry();
    base_.fill(1); // weakly not-taken
    for (auto &table : tage_)
    {
      for (auto &entry : table)
        entry = TageEntry();
    }
    for (auto &table : sc_)
    {
      for (auto &entry : table)
        entry = ScEntry();
    }
    sc_chooser_.fill(0);
    sc_threshold_ = 12;
    sc_threshold_adapt_ = 0;
    for (auto &entry : loop_)
      entry = LoopEntry();

    global_history_ = 0;
    local_history_.fill(0);
    backward_history_ = 0;
    use_alt_on_new_alloc_ = -1;
    usefulness_age_counter_ = 0;
  }

  BranchPrediction PredictorOne::Predict(std::uint32_t pc)
  {
    ++metrics_.lookups;

    const std::uint32_t history_snapshot = global_history_;
    const std::uint16_t local_snapshot = local_history_[LocalHistoryIndex(pc)];
    const std::uint8_t backward_snapshot = backward_history_;

    // ---- Finite direct-indexed BTB ----
    ++component_metrics_.btb_reads;
    const BtbEntry &btb_entry = btb_[BtbIndex(pc)];
    const bool btb_hit = btb_entry.valid && btb_entry.tag == BtbTag(pc);
    if (btb_hit)
    {
      ++metrics_.branches_found;
    }
    else
    {
      ++metrics_.branches_not_found;
    }

    // ---- TAGE: base + eight direct-indexed tagged components ----
    ++component_metrics_.tage_reads; // base table read
    const std::uint8_t base_counter = base_[BaseIndex(pc)];
    const bool base_prediction = base_counter >= 2;

    std::array<std::size_t, kTageTables> tage_indices{};
    std::array<std::uint16_t, kTageTables> tage_tags{};
    std::array<bool, kTageTables> tage_matches{};

    for (std::size_t table = 0; table < kTageTables; ++table)
    {
      tage_indices[table] = TageIndex(pc, history_snapshot, table);
      tage_tags[table] = TageTag(pc, history_snapshot, table);
      const TageEntry &entry = tage_[table][tage_indices[table]];
      ++component_metrics_.tage_reads;
      tage_matches[table] = entry.valid && entry.tag == tage_tags[table];
    }

    int provider_table = -1;
    int alternate_table = -1;
    for (int table = static_cast<int>(kTageTables) - 1; table >= 0; --table)
    {
      if (!tage_matches[static_cast<std::size_t>(table)])
        continue;
      if (provider_table < 0)
      {
        provider_table = table;
      }
      else
      {
        alternate_table = table;
        break;
      }
    }

    const bool provider_prediction = provider_table < 0
                                         ? base_prediction
                                         : tage_[static_cast<std::size_t>(provider_table)]
                                                [tage_indices[static_cast<std::size_t>(provider_table)]]
                                                    .counter >= 0;
    const bool alternate_prediction = alternate_table < 0
                                          ? base_prediction
                                          : tage_[static_cast<std::size_t>(alternate_table)]
                                                 [tage_indices[static_cast<std::size_t>(alternate_table)]]
                                                     .counter >= 0;

    bool tage_prediction = provider_prediction;
    std::uint32_t tage_source =
        provider_table < 0 ? 0U : static_cast<std::uint32_t>(provider_table + 1);
    std::uint32_t tage_confidence = provider_table < 0
                                        ? BaseConfidence(base_counter)
                                        : CounterConfidence(
                                              tage_[static_cast<std::size_t>(provider_table)]
                                                   [tage_indices[static_cast<std::size_t>(provider_table)]]
                                                       .counter);

    if (provider_table >= 0)
    {
      const TageEntry &provider =
          tage_[static_cast<std::size_t>(provider_table)]
               [tage_indices[static_cast<std::size_t>(provider_table)]];
      const bool newly_allocated_or_weak =
          provider.useful == 0 && std::abs(static_cast<int>(provider.counter)) <= 1;
      if (newly_allocated_or_weak &&
          provider_prediction != alternate_prediction &&
          use_alt_on_new_alloc_ >= 0)
      {
        tage_prediction = alternate_prediction;
        tage_source = alternate_table < 0
                          ? 0U
                          : static_cast<std::uint32_t>(alternate_table + 1);
        tage_confidence = alternate_table < 0
                              ? BaseConfidence(base_counter)
                              : CounterConfidence(
                                    tage_[static_cast<std::size_t>(alternate_table)]
                                         [tage_indices[static_cast<std::size_t>(alternate_table)]]
                                             .counter);
      }
    }

    // ---- 2016-style neural/Multi-GEHL statistical corrector ----
    const auto sc_contexts = ScContexts(pc, history_snapshot, local_snapshot,
                                        backward_snapshot, tage_prediction,
                                        tage_confidence);
    const auto sc_indices = ScIndices(sc_contexts);
    const auto sc_tags = ScTags(sc_contexts);
    int sc_score = 0;
    for (std::size_t table = 0; table < kScTables; ++table)
    {
      ++component_metrics_.sc_reads;
      const ScEntry &entry = sc_[table][sc_indices[table]];
      if (entry.valid && entry.tag == sc_tags[table])
        sc_score += entry.counter;
    }
    // Reading the local-history table is an SC-side table access too.
    ++component_metrics_.sc_reads;

    const bool sc_prediction = sc_score >= 0;
    const std::uint32_t sc_strength = static_cast<std::uint32_t>(
        std::min(std::abs(sc_score), 255));
    const bool used_sc = sc_prediction != tage_prediction &&
                         sc_strength >= sc_threshold_ &&
                         sc_chooser_[tage_confidence] >= 0;
    bool conditional_prediction = used_sc ? sc_prediction : tage_prediction;

    // ---- Direct-indexed loop predictor ----
    ++component_metrics_.loop_reads;
    const std::size_t loop_index = LoopIndex(pc);
    const LoopEntry &loop_entry = loop_[loop_index];
    const bool loop_tag_match = loop_entry.valid && loop_entry.tag == LoopTag(pc);
    const bool loop_valid = loop_tag_match && loop_entry.trip_count > 0 &&
                            loop_entry.confidence >= 3;
    bool loop_prediction = conditional_prediction;
    if (loop_valid)
    {
      loop_prediction = loop_entry.current_iteration < loop_entry.trip_count
                            ? loop_entry.direction
                            : !loop_entry.direction;
    }
    const bool used_loop = loop_valid;
    if (used_loop)
      conditional_prediction = loop_prediction;

    const bool btb_unconditional = btb_hit && btb_entry.unconditional;
    bool final_taken = false;
    std::uint32_t final_confidence = 0;
    if (btb_hit)
    {
      if (btb_unconditional)
      {
        final_taken = true;
        final_confidence = 3;
      }
      else
      {
        final_taken = conditional_prediction;
        if (used_loop)
        {
          final_confidence = std::min<std::uint32_t>(3, loop_entry.confidence >= 6 ? 3 : 2);
        }
        else if (used_sc)
        {
          final_confidence = sc_strength >= 24 ? 3 : 2;
        }
        else
        {
          final_confidence = tage_confidence;
        }
      }
    }

    if (final_taken)
    {
      ++metrics_.predicted_taken;
    }
    else
    {
      ++metrics_.predicted_not_taken;
    }

    const std::uint32_t provider_code =
        provider_table < 0 ? 0U : static_cast<std::uint32_t>(provider_table + 1);
    const std::uint32_t alternate_code =
        alternate_table < 0 ? 0U : static_cast<std::uint32_t>(alternate_table + 1);

    BranchPrediction prediction;
    prediction.branch_found = btb_hit;
    prediction.taken = final_taken;
    prediction.target = btb_hit ? btb_entry.target : 0;
    prediction.confidence = PackConfidence(
        final_confidence, tage_confidence, tage_prediction, sc_prediction,
        loop_valid, loop_prediction, used_sc, used_loop,
        alternate_prediction, provider_prediction, sc_strength,
        btb_unconditional, tage_source);
    prediction.information = PackInformation(
        history_snapshot, local_snapshot, backward_snapshot,
        provider_code, alternate_code);
    return prediction;
  }

  void PredictorOne::ObserveInstruction(
      const Instruction &instruction,
      const BranchPrediction &prediction)
  {
    const bool actual_branch = IsBranchOpcode(instruction.opcode);

    if (prediction.branch_found && !actual_branch)
    {
      ++metrics_.false_branch_predictions;

      // A false BTB identification would otherwise repeat indefinitely. Only
      // invalidate when the current direct-mapped entry still represents this PC.
      const std::size_t index = BtbIndex(instruction.pc);
      ++component_metrics_.btb_reads;
      BtbEntry &entry = btb_[index];
      if (entry.valid && entry.tag == BtbTag(instruction.pc))
      {
        entry.valid = false;
        ++component_metrics_.btb_writes;
      }
    }

    if (!prediction.branch_found && actual_branch)
    {
      ++metrics_.missed_branches;
    }
  }

  void PredictorOne::Update(const BranchResult &result,
                            const BranchPrediction &prediction)
  {
    ++metrics_.updates;

    const bool predicted_taken = prediction.branch_found && prediction.taken;
    const bool direction_wrong = predicted_taken != result.taken;
    const bool target_wrong = result.taken && predicted_taken &&
                              prediction.target != result.target;
    if (direction_wrong)
    {
      ++metrics_.direction_mispredictions;
    }
    else if (target_wrong)
    {
      ++metrics_.target_mispredictions;
    }
    else
    {
      ++metrics_.correct_predictions;
    }

    // ---- Train/allocate the finite direct-indexed BTB for every real branch ----
    const std::size_t btb_index = BtbIndex(result.pc);
    ++component_metrics_.btb_reads;
    BtbEntry &btb_entry = btb_[btb_index];
    btb_entry.valid = true;
    btb_entry.tag = BtbTag(result.pc);
    btb_entry.target = result.target;
    btb_entry.unconditional = result.type == BranchType::kUnconditional;
    ++component_metrics_.btb_writes;

    if (result.type != BranchType::kConditional)
      return;

    const std::uint32_t global_snapshot =
        SavedGlobalHistory(prediction.information);
    const std::uint16_t local_snapshot =
        SavedLocalHistory(prediction.information);
    const std::uint8_t backward_snapshot =
        SavedBackwardHistory(prediction.information);
    const std::uint32_t provider_code =
        SavedProviderCode(prediction.information);
    const std::uint32_t alternate_code =
        SavedAlternateCode(prediction.information);

    const bool tage_prediction = SavedTagePrediction(prediction.confidence);
    const bool sc_prediction = SavedScPrediction(prediction.confidence);
    const bool loop_valid = SavedLoopValid(prediction.confidence);
    const bool loop_prediction = SavedLoopPrediction(prediction.confidence);
    const bool used_sc = SavedUsedSc(prediction.confidence);
    const bool used_loop = SavedUsedLoop(prediction.confidence);
    const bool alternate_prediction =
        SavedAlternatePrediction(prediction.confidence);
    const bool provider_prediction =
        SavedProviderPrediction(prediction.confidence);
    const std::uint32_t tage_confidence =
        SavedTageConfidence(prediction.confidence);
    const std::uint32_t sc_strength =
        SavedScStrength(prediction.confidence);
    const std::uint32_t tage_source = SavedTageSource(prediction.confidence);

    // Component-level report counters are counted only for resolved conditional
    // branches, not for ordinary instruction fetches.
    ++component_metrics_.tage_predictions;
    if (tage_prediction == result.taken)
      ++component_metrics_.tage_correct;
    if (tage_source < component_metrics_.tage_source_predictions.size())
    {
      ++component_metrics_.tage_source_predictions[tage_source];
    }

    ++component_metrics_.sc_predictions;
    if (sc_prediction == result.taken)
      ++component_metrics_.sc_correct;
    if (used_sc)
    {
      ++component_metrics_.sc_overrides;
      if (sc_prediction == result.taken)
      {
        ++component_metrics_.sc_override_correct;
      }
    }
    if (used_loop && loop_valid)
    {
      ++component_metrics_.loop_selections;
      if (loop_prediction == result.taken)
        ++component_metrics_.loop_correct;
    }

    // ---- TAGE training ----
    const std::size_t base_index = BaseIndex(result.pc);
    const int provider_table = provider_code == 0
                                   ? -1
                                   : static_cast<int>(provider_code - 1);
    const int alternate_table = alternate_code == 0
                                    ? -1
                                    : static_cast<int>(alternate_code - 1);

    auto update_base = [&]()
    {
      ++component_metrics_.tage_reads;
      UpdateBaseCounter(&base_[base_index], result.taken);
      ++component_metrics_.tage_writes;
    };

    bool provider_still_matches = false;
    if (provider_table < 0)
    {
      update_base();
      provider_still_matches = true;
    }
    else if (provider_table < static_cast<int>(kTageTables))
    {
      const std::size_t table = static_cast<std::size_t>(provider_table);
      const std::size_t index = TageIndex(result.pc, global_snapshot, table);
      const std::uint16_t tag = TageTag(result.pc, global_snapshot, table);
      ++component_metrics_.tage_reads;
      TageEntry &provider = tage_[table][index];
      if (provider.valid && provider.tag == tag)
      {
        provider_still_matches = true;
        UpdateSignedCounter(&provider.counter, result.taken,
                            kTageCounterMin, kTageCounterMax);
        ++component_metrics_.tage_writes;

        if (provider_prediction != alternate_prediction)
        {
          if (provider_prediction == result.taken)
          {
            if (provider.useful < 3)
            {
              ++provider.useful;
              ++component_metrics_.tage_writes;
            }
          }
          else if (provider.useful > 0)
          {
            --provider.useful;
            ++component_metrics_.tage_writes;
          }
        }
      }
    }

    // If the alternate actually supplied TAGE's output for a weak/new provider,
    // train it as well. This mirrors the USE_ALT_ON_NA behavior of TAGE.
    if (provider_table >= 0 && tage_source != provider_code)
    {
      if (alternate_table < 0)
      {
        update_base();
      }
      else if (alternate_table < static_cast<int>(kTageTables))
      {
        const std::size_t table = static_cast<std::size_t>(alternate_table);
        const std::size_t index = TageIndex(result.pc, global_snapshot, table);
        const std::uint16_t tag = TageTag(result.pc, global_snapshot, table);
        ++component_metrics_.tage_reads;
        TageEntry &alternate = tage_[table][index];
        if (alternate.valid && alternate.tag == tag)
        {
          UpdateSignedCounter(&alternate.counter, result.taken,
                              kTageCounterMin, kTageCounterMax);
          ++component_metrics_.tage_writes;
        }
      }
    }

    if (provider_table >= 0 && provider_prediction != alternate_prediction)
    {
      ++component_metrics_.tage_reads;
      const std::size_t provider_index = TageIndex(
          result.pc, global_snapshot, static_cast<std::size_t>(provider_table));
      const TageEntry &provider =
          tage_[static_cast<std::size_t>(provider_table)][provider_index];
      if (provider_still_matches && provider.useful == 0 &&
          std::abs(static_cast<int>(provider.counter)) <= 1)
      {
        if (alternate_prediction == result.taken &&
            provider_prediction != result.taken)
        {
          UpdateChooser(&use_alt_on_new_alloc_, true);
        }
        else if (provider_prediction == result.taken &&
                 alternate_prediction != result.taken)
        {
          UpdateChooser(&use_alt_on_new_alloc_, false);
        }
      }
    }

    // Allocate longer-history entries when TAGE itself was wrong. At most two
    // entries are allocated; if all candidates are protected, age usefulness so
    // later misses can replace stale entries.
    if (tage_prediction != result.taken)
    {
      const std::size_t first_longer = provider_table < 0
                                           ? 0U
                                           : static_cast<std::size_t>(provider_table + 1);
      std::size_t allocated = 0;
      bool saw_candidate = false;

      for (std::size_t table = first_longer;
           table < kTageTables && allocated < 2; ++table)
      {
        const std::size_t index = TageIndex(result.pc, global_snapshot, table);
        const std::uint16_t tag = TageTag(result.pc, global_snapshot, table);
        ++component_metrics_.tage_reads;
        TageEntry &entry = tage_[table][index];
        saw_candidate = true;

        if (!entry.valid || entry.useful == 0)
        {
          entry.valid = true;
          entry.tag = tag;
          entry.counter = result.taken ? 0 : -1; // weak in actual direction
          entry.useful = 0;
          ++component_metrics_.tage_writes;
          ++allocated;
        }
      }

      if (allocated == 0 && saw_candidate)
      {
        for (std::size_t table = first_longer; table < kTageTables; ++table)
        {
          const std::size_t index = TageIndex(result.pc, global_snapshot, table);
          ++component_metrics_.tage_reads;
          TageEntry &entry = tage_[table][index];
          if (entry.useful > 0)
          {
            --entry.useful;
            ++component_metrics_.tage_writes;
          }
        }
      }
    }

    // Slow global usefulness aging prevents old entries from becoming immortal.
    ++usefulness_age_counter_;
    if (usefulness_age_counter_ >= 1024)
    {
      usefulness_age_counter_ = 0;
      for (auto &table : tage_)
      {
        for (auto &entry : table)
        {
          ++component_metrics_.tage_reads;
          if (entry.useful > 0)
          {
            --entry.useful;
            ++component_metrics_.tage_writes;
          }
        }
      }
    }

    // ---- Statistical-corrector training ----
    const auto sc_contexts = ScContexts(result.pc, global_snapshot, local_snapshot,
                                        backward_snapshot, tage_prediction,
                                        tage_confidence);
    const auto sc_indices = ScIndices(sc_contexts);
    const auto sc_tags = ScTags(sc_contexts);
    const bool train_sc = sc_prediction != result.taken ||
                          sc_strength < sc_threshold_;
    if (train_sc)
    {
      for (std::size_t table = 0; table < kScTables; ++table)
      {
        ++component_metrics_.sc_reads;
        ScEntry &entry = sc_[table][sc_indices[table]];
        if (!entry.valid || entry.tag != sc_tags[table])
        {
          entry.valid = true;
          entry.tag = sc_tags[table];
          entry.counter = result.taken ? 1 : -1;
        }
        else
        {
          UpdateSignedCounter(&entry.counter, result.taken,
                              kScCounterMin, kScCounterMax);
        }
        ++component_metrics_.sc_writes;
      }
    }

    // Dynamic training threshold: increase it when a confident-looking SC is
    // wrong; lower it after repeated correct-but-weak decisions.
    if (sc_prediction != result.taken)
    {
      if (sc_threshold_adapt_ < 31)
        ++sc_threshold_adapt_;
    }
    else if (sc_strength < sc_threshold_)
    {
      if (sc_threshold_adapt_ > -32)
        --sc_threshold_adapt_;
    }
    if (sc_threshold_adapt_ >= 8)
    {
      if (sc_threshold_ < 31)
        ++sc_threshold_;
      sc_threshold_adapt_ = 0;
    }
    else if (sc_threshold_adapt_ <= -8)
    {
      if (sc_threshold_ > 6)
        --sc_threshold_;
      sc_threshold_adapt_ = 0;
    }

    if (sc_prediction != tage_prediction)
    {
      if (sc_prediction == result.taken && tage_prediction != result.taken)
      {
        UpdateChooser(&sc_chooser_[tage_confidence], true);
      }
      else if (tage_prediction == result.taken && sc_prediction != result.taken)
      {
        UpdateChooser(&sc_chooser_[tage_confidence], false);
      }
    }

    // ---- Loop-predictor update/replacement ----
    const std::size_t loop_index = LoopIndex(result.pc);
    const std::uint16_t loop_tag = LoopTag(result.pc);
    ++component_metrics_.loop_reads;
    LoopEntry &loop_entry = loop_[loop_index];

    if (!loop_entry.valid || loop_entry.tag != loop_tag)
    {
      if (!loop_entry.valid || loop_entry.age == 0)
      {
        loop_entry.valid = true;
        loop_entry.tag = loop_tag;
        loop_entry.direction = result.taken;
        loop_entry.trip_count = 0;
        loop_entry.current_iteration = 1;
        loop_entry.confidence = 0;
        loop_entry.age = 1;
        ++component_metrics_.loop_writes;
      }
      else
      {
        --loop_entry.age;
        ++component_metrics_.loop_writes;
      }
    }
    else if (result.taken == loop_entry.direction)
    {
      if (loop_entry.current_iteration < kLoopCounterMax)
      {
        ++loop_entry.current_iteration;
      }
      if (loop_entry.trip_count > 0 &&
          loop_entry.current_iteration > loop_entry.trip_count)
      {
        // The loop lasted longer than the learned trip count: its behavior has
        // changed, so confidence must be rebuilt from fresh observations.
        loop_entry.trip_count = 0;
        loop_entry.confidence = 0;
      }
      ++component_metrics_.loop_writes;
    }
    else
    {
      const std::uint16_t observed_trip_count = loop_entry.current_iteration;
      if (observed_trip_count == 0)
      {
        // The initial direction guess was wrong; restart with the observed one.
        loop_entry.direction = result.taken;
        loop_entry.current_iteration = 1;
        loop_entry.trip_count = 0;
        loop_entry.confidence = 0;
      }
      else if (loop_entry.trip_count == observed_trip_count)
      {
        if (loop_entry.confidence < 7)
          ++loop_entry.confidence;
        if (loop_entry.age < 7)
          ++loop_entry.age;
        loop_entry.current_iteration = 0;
      }
      else
      {
        loop_entry.trip_count = observed_trip_count;
        loop_entry.current_iteration = 0;
        loop_entry.confidence = 0;
        if (loop_entry.age > 0)
          --loop_entry.age;
      }
      ++component_metrics_.loop_writes;
    }

    // ---- Resolution-only history update ----
    global_history_ = (global_history_ << 1) | (result.taken ? 1U : 0U);

    const std::size_t local_index = LocalHistoryIndex(result.pc);
    ++component_metrics_.sc_reads;
    local_history_[local_index] = static_cast<std::uint16_t>(
        (local_history_[local_index] << 1) | (result.taken ? 1U : 0U));
    ++component_metrics_.sc_writes;

    if (result.target < result.pc)
    {
      backward_history_ = static_cast<std::uint8_t>(
          (backward_history_ << 1) | (result.taken ? 1U : 0U));
    }
  }

  std::uint64_t PredictorOne::StorageBits() const
  {
    // Logical, provisioned storage (not sizeof(C++ objects)). This intentionally
    // counts the declared hardware-width fields, not host padding/alignment.
    const std::uint64_t btb_bits =
        kBtbEntries * (1ULL + 16ULL + 32ULL + 1ULL);
    const std::uint64_t base_bits = kBaseEntries * 2ULL;
    const std::uint64_t tage_bits =
        kTageTables * kTageEntries * (1ULL + 10ULL + 3ULL + 2ULL);
    const std::uint64_t global_history_bits = 32;
    const std::uint64_t local_history_bits = kLocalHistoryEntries * 16ULL;
    const std::uint64_t backward_history_bits = 8;
    const std::uint64_t sc_bits =
        kScTables * kScEntries * (1ULL + 6ULL + 6ULL);
    const std::uint64_t sc_chooser_bits = 4ULL * 4ULL;
    const std::uint64_t sc_threshold_bits = 6ULL + 6ULL;
    const std::uint64_t loop_bits =
        kLoopEntries * (1ULL + 12ULL + 1ULL + 12ULL + 12ULL + 3ULL + 3ULL);
    const std::uint64_t tage_control_bits = 4ULL + 10ULL;

    return btb_bits + base_bits + tage_bits + global_history_bits +
           local_history_bits + backward_history_bits + sc_bits +
           sc_chooser_bits + sc_threshold_bits + loop_bits +
           tage_control_bits;
  }

  const PredictorMetrics &PredictorOne::Metrics() const
  {
    return metrics_;
  }

  const PredictorOneComponentMetrics &PredictorOne::ComponentMetrics() const
  {
    return component_metrics_;
  }

}
