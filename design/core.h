#pragma once
#include "pipesim.h"
#include "execution_unit.h"
#include "design/control.h"
#include "branch_prediction.h"
#include "speculation.h"
#include <array>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
namespace pipesim::design_internal {
struct FrontendLatch { bool valid=false; FetchedInstruction fetched; bool decoded=false; bool control_recognized=false; DecodeResult decode; };
enum class RegisterKind { kPc, kIfId, kIdEx, kExMem, kMemWb };
class Core {
 public:
  Core(Machine*, const std::array<ExecutionPipeline*,7>&, MainControl*, HazardControl*, BranchPredictionSystem*, SpeculationController*);
  void ResetRegister(RegisterKind); void CommitRegister(RegisterKind); std::string RegisterSnapshot(RegisterKind) const;
  void ResetStage(StageRole); StageSnapshot Snapshot(StageRole) const;
  void Writeback(); void Memory(); void Execute(); void Decode(); void Fetch();
 private:
  void BeginCycle();
  void FinalizePipelineSquash();
  static StageSnapshot SnapshotFor(const Instruction&);
  Machine* machine_; std::array<ExecutionPipeline*,7> pipelines_; MainControl* main_control_; HazardControl* hazard_control_;
  BranchPredictionSystem* branch_prediction_; SpeculationController* speculation_control_;
  std::uint32_t pc_=0,next_pc_=0; bool fetch_stopped_=false,next_fetch_stopped_=false;
  FrontendLatch if_id_,next_if_id_; std::uint64_t current_boundary_=0;
  std::array<bool,7> consume_output_={}; bool if_id_consumed_=false; bool control_recognized_this_cycle_=false; bool pipelines_operated_=false; bool cycle_begun_=false;
  bool control_stall_recorded_=false;
  std::optional<std::uint64_t> squash_after_sequence_;
  StageSnapshot snapshots_[5];
  bool Dispatch(size_t& idx, ExecutionOperation& op);
};
}
