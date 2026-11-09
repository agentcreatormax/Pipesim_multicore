#include "design/control.h"
#include <stdexcept>
namespace pipesim::design_internal {
DecodeResult MainControl::Decode(const Instruction& instruction) { Touch(); return {UnitFor(instruction.opcode), WritesRegister(instruction)}; }
HazardResult HazardControl::Check(Machine* machine,std::uint64_t sequence,const Instruction& instruction,bool writes_register){ 
    Touch(); 
    if(ReadsRs1(instruction.opcode)&&machine->HasOlderProducer(sequence,instruction.rs1)) return HazardResult::kRaw; 

    //Start

    if(ReadsRs2(instruction.opcode)&&machine->HasOlderProducer(sequence,instruction.rs2)) return HazardResult::kRaw;
    if(writes_register&&machine->HasOlderDestination(sequence,instruction.rd))return HazardResult::kWaw;
    return HazardResult::kNone;

    //End

}
}
