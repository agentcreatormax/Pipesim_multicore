#include "speculation/internal.h"

#include <stdexcept>

namespace pipesim {

SpeculationController::SpeculationController(std::size_t maximum_checkpoints)
    : impl_(new Impl(maximum_checkpoints)) {
  if (maximum_checkpoints == 0) {
    throw std::runtime_error("maximum checkpoints must be positive");
  }
}

SpeculationController::~SpeculationController() = default;

void SpeculationController::Reset() {
  //Start
  impl_->checkpoints.clear();
  //End
}

bool SpeculationController::CanFetch() const {
  //Start
  return impl_->checkpoints.size()<impl_->maximum_checkpoints;
  //End
}

bool SpeculationController::CanDispatch(
    std::uint64_t sequence,
    const Instruction& instruction) const {
  //Start
  for(auto x:impl_->checkpoints){
    if(x.sequence<sequence){
      Opcode op=instruction.opcode;
      return !((op==Opcode::kLw)||(op==Opcode::kSw)||(op==Opcode::kBeq)||
              (op==Opcode::kBne)||(op==Opcode::kJ)||(op==Opcode::kHalt)||
              (op==Opcode::kFence)||(op==Opcode::kXchg));
    }
  }
  return true;
  //End
}

void SpeculationController::AddCheckpoint(
    const BranchCheckpoint& checkpoint) {
  //Start
  if(impl_->checkpoints.size()>=impl_->maximum_checkpoints){
    throw std::runtime_error("SpeculationController::AddCheckpoint: checkpoint capacity exceeded");
  }
  impl_->checkpoints.push_back(checkpoint);
  //End
}

bool SpeculationController::IsSpeculative(std::uint64_t sequence) const {
  //Start
  for(auto x:impl_->checkpoints){
    if(x.sequence<sequence)return true;
  }
  return false;
  //End
}

std::uint64_t SpeculationController::SpeculationDepth(
    std::uint64_t sequence) const {
  //Start
  uint64_t depth=0;
  for(auto x:impl_->checkpoints){
    if(x.sequence<sequence)depth++;
  }
  return depth;
  //End
}

RecoveryDecision SpeculationController::Resolve(const BranchResult& result) {
  //Start
  auto it=impl_->checkpoints.begin();
  while(it!=impl_->checkpoints.end() && it->sequence!=result.sequence){
    it++;
  }
  if(it==impl_->checkpoints.end()) {
    throw std::runtime_error("SpeculationController::Resolve: checkpoint not found");
  }
  BranchCheckpoint cp=*it;
  impl_->checkpoints.erase(it);
  uint32_t next_pc=result.taken?result.target:result.pc+4;
  RecoveryDecision decision;
  decision.mispredicted=(next_pc!=cp.predicted_next_pc);
  decision.branch_sequence=result.sequence;
  decision.correct_next_pc=next_pc;
  return decision;
  //End
}

void SpeculationController::RemoveYoungerCheckpoints(std::uint64_t sequence) {
  //Start
  auto it=impl_->checkpoints.begin();
  while(it!=impl_->checkpoints.end()){
    if(it->sequence>sequence)it=impl_->checkpoints.erase(it);
    else it++;
  }
  //End
}

std::size_t SpeculationController::CheckpointCount() const {
  return impl_->checkpoints.size();
}

}
