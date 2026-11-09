#include "design/core.h"
#include "design/instruction_helpers.h"
#include <limits>
#include <stdexcept>
namespace pipesim::design_internal {
Core::Core(Machine* machine,const std::array<ExecutionPipeline*,7>& pipelines,MainControl* main_control,HazardControl* hazard_control,BranchPredictionSystem* branch_prediction,SpeculationController* speculation_control):machine_(machine),pipelines_(pipelines),main_control_(main_control),hazard_control_(hazard_control),branch_prediction_(branch_prediction),speculation_control_(speculation_control){
 if(machine_==nullptr||main_control_==nullptr||hazard_control_==nullptr||branch_prediction_==nullptr||speculation_control_==nullptr) throw std::runtime_error("CPU core received a null component");
 for(std::size_t i=0;i<pipelines_.size();++i) if(pipelines_[i]==nullptr||pipelines_[i]->kind()!=static_cast<ExecutionUnitKind>(i)) throw std::runtime_error("CPU core execution-unit registration mismatch");
}
void Core::ResetRegister(RegisterKind kind){ if(kind!=RegisterKind::kPc)return; pc_=machine_->EntryPoint(); next_pc_=pc_; fetch_stopped_=false; next_fetch_stopped_=false; if_id_=FrontendLatch(); next_if_id_=FrontendLatch(); current_boundary_=0; cycle_begun_=false; squash_after_sequence_.reset(); for(auto& s:snapshots_)s=StageSnapshot(); }
void Core::CommitRegister(RegisterKind kind){ if(kind!=RegisterKind::kPc)return; if(!cycle_begun_||!pipelines_operated_) throw std::runtime_error("CPU core commit without a complete operate pass"); pc_=next_pc_; fetch_stopped_=next_fetch_stopped_; if_id_=next_if_id_; if(current_boundary_==std::numeric_limits<std::uint64_t>::max()) throw std::runtime_error("CPU boundary counter overflow"); ++current_boundary_; cycle_begun_=false; }
std::string Core::RegisterSnapshot(RegisterKind kind) const { std::ostringstream output; switch(kind){ case RegisterKind::kPc: output<<"pc=0x"<<std::hex<<pc_<<std::dec<<" stopped="<<(fetch_stopped_?1:0); return output.str(); case RegisterKind::kIfId: if(!if_id_.valid)return "EMPTY"; output<<OpcodeName(if_id_.fetched.instruction.opcode)<<"@0x"<<std::hex<<if_id_.fetched.instruction.pc<<std::dec<<"#"<<if_id_.fetched.sequence; return output.str(); case RegisterKind::kIdEx:return "EXECUTION_PIPELINES"; case RegisterKind::kExMem:return "AGU_HANDOFF"; case RegisterKind::kMemWb:return "COMPLETION_QUEUE";} return "EMPTY"; }
void Core::ResetStage(StageRole role){ snapshots_[static_cast<std::size_t>(role)]=StageSnapshot(); }
StageSnapshot Core::Snapshot(StageRole role) const{return snapshots_[static_cast<std::size_t>(role)];}
void Core::Writeback(){ 
    BeginCycle(); 

    //Start

    StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kWriteback)];
    snapshot=StageSnapshot();
    snapshot.action="EMPTY";
    Retirement r;
    if(!machine_->PeekRetirement(&r))return;
    machine_->ApplyRetirement(r);
    snapshot=SnapshotFor(r.instruction);
    snapshot.action="RETIRE";

    //End

}
void Core::Memory(){ 
    StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kMemory)]; 
    snapshot=StageSnapshot(); 
    snapshot.action="EMPTY"; 

    //Start

    size_t i=(size_t)ExecutionUnitKind::kAgu;
    std::optional<ExecutionResult>out=pipelines_[i]->OutputIntent();
    if(!out.has_value())return;
    snapshot=SnapshotFor(out->instruction);
    snapshot.action="PENDING";
    if(!machine_->MemorySequenceMayIssue(out->sequence))return;
    if(!machine_->PendingDataHasCapacity()){
        machine_->RecordCpuStall(CpuStallReason::kPendingDataFull);
        return;
    }
    if(!machine_->DataRequestLinkCanAccept()){
        machine_->RecordCpuStall(CpuStallReason::kDcacheInput);
        return;
    }
    if(machine_->TryIssueData(out->sequence,out->address,out->memory_write,out->store_value)){
        consume_output_[i]=1;
        snapshot.action="ISSUE";
    }

    //End
 
}
bool Core::Dispatch(size_t& idx,ExecutionOperation& op){
    if(!if_id_.valid)return 0;
    Instruction instr=if_id_.fetched.instruction;
    uint64_t seq=if_id_.fetched.sequence;
    if(!machine_->CompletionQueueHasCapacity()){
        machine_->RecordCpuStall(CpuStallReason::kCompletionQueueFull);
        return 0;
    }
    DecodeResult dec=main_control_->Decode(instr);
    bool write=dec.writes_register;
    HazardResult hazard=hazard_control_->Check(machine_,seq,instr,write);
    if(hazard==HazardResult::kRaw){
        machine_->RecordCpuStall(CpuStallReason::kOutstandingRaw);
        return 0;
    }
    if(hazard==HazardResult::kWaw){
        machine_->RecordCpuStall(CpuStallReason::kOutstandingWaw);
        return 0;
    }
    idx=(size_t)dec.unit;
    ExecutionPipeline*  pipeline=pipelines_[idx];
    uint64_t boundary=2+current_boundary_+pipeline->config().latency;
    bool agu=(dec.unit==ExecutionUnitKind::kAgu);
    if (!pipeline->CanAccept(1+current_boundary_,consume_output_[idx])) {
        machine_->RecordCpuStall(CpuStallReason::kExecutionUnitBusy);
        return 0;
    }
    if(!agu && !machine_->NormalCompletionSlotAvailable(boundary)){
        machine_->RecordCpuStall(CpuStallReason::kExecutionCompletionSlot);
        return 0;
    }
    op.sequence=seq;
    op.instruction=instr;
    op.lhs=ReadsRs1(instr.opcode)?machine_->ReadRegister(instr.rs1):0;
    op.rhs=(agu||!ReadsRs2(instr.opcode))?static_cast<uint32_t>(instr.immediate):machine_->ReadRegister(instr.rs2);
    op.store_value=ReadsRs2(instr.opcode)?machine_->ReadRegister(instr.rs2):0;
    op.dispatch_boundary=1+current_boundary_;
    op.fetch_cycle=if_id_.fetched.fetch_cycle;
    op.predictions=if_id_.fetched.predictions;
    std::optional<uint64_t>s=agu?std::nullopt:std::optional<uint64_t>(boundary);
    machine_->TryAllocateCompletion(seq,instr,write,s);
    if_id_consumed_=1;
    return 1;
}
void Core::Execute(){ 
    StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kExecute)]; 
    snapshot=StageSnapshot(); 
    snapshot.action="EMPTY"; 

    //Start 

    for(size_t i=0;i<pipelines_.size();i++){
        ExecutionPipeline* p=pipelines_[i];
        std::optional<ExecutionResult>out=p->OutputIntent();
        if(!out.has_value())continue;
        if(p->kind()==ExecutionUnitKind::kAgu)continue;
        consume_output_[i]=1;
        machine_->CompleteNonMemory(out->sequence,out->value,out->halt);
        snapshot=SnapshotFor(out->instruction);
        if(p->kind()==ExecutionUnitKind::kControl){
            BranchType type=BranchType::kNotBranch;
            if(out->instruction.opcode==Opcode::kBeq || out->instruction.opcode==Opcode::kBne){
                type=BranchType::kConditional;
            } 
            else if(out->instruction.opcode==Opcode::kJ){
                type=BranchType::kUnconditional;
            } 
            else if(out->instruction.opcode==Opcode::kHalt){
                type=BranchType::kHalt;
            }
            if(type!=BranchType::kHalt){
                BranchResult br;
                br.sequence=out->sequence;
                br.fetch_cycle=out->fetch_cycle;
                br.resolve_cycle=current_boundary_;
                br.pc=out->instruction.pc;
                br.type=type;
                br.taken=out->redirect;
                // BranchResult::target is the branch destination itself, not
                // the actual next PC on a not-taken conditional branch.
                br.target=static_cast<std::uint32_t>(out->instruction.immediate);
                branch_prediction_->Resolve(br,out->predictions);
                RecoveryDecision decision=speculation_control_->Resolve(br);
                if(decision.mispredicted){
                    next_pc_=decision.correct_next_pc;
                    SquashReport report=machine_->SquashAfter(out->sequence);
                    uint64_t sq=0;
                    for(size_t j=0;j<pipelines_.size();j++){
                        sq+= pipelines_[j]->SquashAfter(out->sequence);
                    }
                    speculation_control_->RemoveYoungerCheckpoints(out->sequence);
                    if(next_if_id_.valid && next_if_id_.fetched.sequence>out->sequence){
                        next_if_id_=FrontendLatch();
                    }
                    branch_prediction_->RecordSquash(report.fetches,sq,report.completions,report.data_requests);
                    branch_prediction_->RecordRecoveryCycle();
                    squash_after_sequence_=out->sequence;
                    snapshot.action="REDIRECT";
                } 
                else snapshot.action = "RESOLVE";
                next_fetch_stopped_=0;
            } 
            else snapshot.action="HALT";
        }
    }

    //End

}
void Core::Decode(){ 
    StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kDecode)]; 
    snapshot=StageSnapshot(); 
    snapshot.action="EMPTY";

    // A branch may have resolved in Execute earlier in this same cycle.  The
    // current IF/ID latch still contains the old current-state value, so never
    // decode/dispatch it if it is younger than that recovery boundary.
    if(squash_after_sequence_.has_value() && if_id_.valid &&
       if_id_.fetched.sequence>*squash_after_sequence_){
        next_if_id_=FrontendLatch();
        for(size_t j=0;j<pipelines_.size();j++){
            pipelines_[j]->Operate(current_boundary_,std::nullopt,consume_output_[j]);
        }
        FinalizePipelineSquash();
        pipelines_operated_=1;
        return;
    }

    if(!if_id_.valid){
        for(size_t j=0;j<pipelines_.size();j++){
            pipelines_[j]->Operate(current_boundary_,std::nullopt,consume_output_[j]);
        }
        FinalizePipelineSquash();
        pipelines_operated_=1;
        return;
    }

    if(!if_id_.decoded){
        const bool false_branch=branch_prediction_->ObserveInstruction(
            if_id_.fetched.instruction,if_id_.fetched.predictions);
        next_if_id_.decoded=1;
        if(false_branch){
            // Decode has discovered that the BTB sent fetch down a false branch.
            // Preserve this real instruction, but discard every younger dynamic
            // instruction/fetch and restart at the sequential PC.
            next_pc_=if_id_.fetched.instruction.pc+4U;
            next_fetch_stopped_=false;
            SquashReport report=machine_->SquashAfter(if_id_.fetched.sequence);
            std::uint64_t sq=0;
            for(size_t j=0;j<pipelines_.size();j++){
                sq+=pipelines_[j]->SquashAfter(if_id_.fetched.sequence);
            }
            speculation_control_->RemoveYoungerCheckpoints(if_id_.fetched.sequence);
            branch_prediction_->RecordSquash(report.fetches,sq,report.completions,report.data_requests);
            branch_prediction_->RecordRecoveryCycle();
            squash_after_sequence_=if_id_.fetched.sequence;
        }
    }

    const Opcode opcode=if_id_.fetched.instruction.opcode;
    const bool is_branch=(opcode==Opcode::kBeq || opcode==Opcode::kBne || opcode==Opcode::kJ);

    // HALT is a control-pipeline instruction but is explicitly not a branch.
    // Stop issuing new fetches once the real HALT reaches decode; if it is later
    // squashed by an older branch recovery, Execute clears this stop state.
    if(opcode==Opcode::kHalt){
        next_fetch_stopped_=true;
    }

    if(is_branch && !control_recognized_this_cycle_ && !if_id_.control_recognized){
        // Checkpoint capacity exhaustion is a normal stall, not an exception.
        if(!speculation_control_->CanFetch()){
            if(!control_stall_recorded_){ machine_->RecordCpuStall(CpuStallReason::kControlDisabled); control_stall_recorded_=true; }
            for(size_t j=0;j<pipelines_.size();j++){
                pipelines_[j]->Operate(current_boundary_,std::nullopt,consume_output_[j]);
            }
            FinalizePipelineSquash();
            pipelines_operated_=1;
            return;
        }
        control_recognized_this_cycle_=1;
        next_if_id_.control_recognized=1;
        BranchCheckpoint cp;
        cp.sequence=if_id_.fetched.sequence;
        cp.predicted_next_pc=if_id_.fetched.predictions.next_pc;
        cp.predictions=if_id_.fetched.predictions;
        speculation_control_->AddCheckpoint(cp);
    }

    if(!speculation_control_->CanDispatch(if_id_.fetched.sequence,if_id_.fetched.instruction)){
        if(!control_stall_recorded_){ machine_->RecordCpuStall(CpuStallReason::kControlDisabled); control_stall_recorded_=true; }
        for(size_t j=0;j<pipelines_.size();j++){
            pipelines_[j]->Operate(current_boundary_,std::nullopt,consume_output_[j]);
        }
        FinalizePipelineSquash();
        pipelines_operated_=1;
        return;
    }

    size_t i=0;
    ExecutionOperation op;
    const bool flag=Dispatch(i,op);
    if(flag){
        snapshot.action="ISSUE";
        if(speculation_control_->IsSpeculative(if_id_.fetched.sequence)){
            branch_prediction_->RecordSpeculativeDispatch();
        }
    }
    for(size_t j=0;j<pipelines_.size();j++){
        std::optional<ExecutionOperation>d=std::nullopt;
        if(flag && i==j)d=op;
        pipelines_[j]->Operate(current_boundary_,d,consume_output_[j]);
    }
    FinalizePipelineSquash();
    pipelines_operated_=1;
}
void Core::Fetch(){
    StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kFetch)];
    snapshot=StageSnapshot(); 
    snapshot.pc=pc_;

    if((!if_id_.valid || if_id_consumed_)){
        FetchedInstruction i;
        if(!(fetch_stopped_ || next_fetch_stopped_) && machine_->PeekFetched(&i)){
            machine_->ConsumeFetched();
            next_if_id_.valid=true;
            next_if_id_.fetched=i;
            next_if_id_.decoded=false;
            next_if_id_.control_recognized=false;
            snapshot=SnapshotFor(i.instruction);
            snapshot.action="FETCH";
        } 
        else{
            next_if_id_=FrontendLatch();
            snapshot.action="EMPTY";
        }
    }
    if(fetch_stopped_ || next_fetch_stopped_){ 
        if(!control_stall_recorded_){ machine_->RecordCpuStall(CpuStallReason::kControlDisabled); control_stall_recorded_=true; }
        if(snapshot.action!="FETCH")snapshot.action="BUBBLE";
        return;
    }
    if(!speculation_control_->CanFetch()){
        if(!control_stall_recorded_){ machine_->RecordCpuStall(CpuStallReason::kControlDisabled); control_stall_recorded_=true; }
        return;
    }
    if(!machine_->FetchQueueHasCapacity()){
        machine_->RecordCpuStall(CpuStallReason::kFetchQueueFull);
        return;
    }
    if(!machine_->FetchRequestLinkCanAccept()){
        machine_->RecordCpuStall(CpuStallReason::kIcacheInput);
        return;
    }
    BranchPredictionSet p=branch_prediction_->Predict(next_pc_,current_boundary_);
    if(machine_->TryIssuePredictedFetch(next_pc_,p,current_boundary_)){
        if(speculation_control_->CheckpointCount()!=0){
            branch_prediction_->RecordSpeculativeFetch(
                static_cast<std::uint64_t>(speculation_control_->CheckpointCount()));
        }
        next_pc_=p.next_pc;
    }
}
void Core::BeginCycle(){ if(cycle_begun_) throw std::runtime_error("CPU logic cycle initialized twice"); next_pc_=pc_; next_fetch_stopped_=fetch_stopped_; next_if_id_=if_id_; consume_output_.fill(false); if_id_consumed_=false; control_recognized_this_cycle_=false; pipelines_operated_=false; control_stall_recorded_=false; squash_after_sequence_.reset(); cycle_begun_=true; }
void Core::FinalizePipelineSquash(){
    if(!squash_after_sequence_.has_value())return;
    for(size_t j=0;j<pipelines_.size();++j){
        // Execute already counted the original wrong-path operations.  This
        // second pass runs after Operate so stale current-state entries cannot
        // be copied back into next-state and resurrected.
        pipelines_[j]->SquashAfter(*squash_after_sequence_);
    }
}
StageSnapshot Core::SnapshotFor(const Instruction& instruction){ StageSnapshot snapshot; snapshot.valid=true; snapshot.pc=instruction.pc; snapshot.opcode=OpcodeName(instruction.opcode); return snapshot; }
}
