#include "framework/internal.h"

namespace pipesim {

    using namespace framework_internal;

    bool Machine::PeekRetirement(Retirement* retirement) const {

        if (impl_->completion_current.empty() ||       !impl_->completion_current.front().ready) {

            return false;

        }

        const Impl::Completion& entry = impl_->completion_current.front();

        if (retirement != nullptr) {

            retirement->sequence = entry.sequence;

            retirement->instruction = entry.instruction;

            retirement->writes_register = entry.writes_register;

            retirement->value = entry.value;

            retirement->halt = entry.halt;

        }

        return true;

    }

    void Machine::ApplyRetirement(const Retirement& retirement) {

        if (impl_->completion_current.empty() ||       !impl_->completion_current.front().ready ||       impl_->completion_next.empty() ||       impl_->completion_next.front().sequence !=           impl_->completion_current.front().sequence ||       impl_->retirement_planned) {

            throw std::runtime_error("retirement called without the current ready head");

        }

        const Impl::Completion& head = impl_->completion_current.front();

        if (retirement.sequence != head.sequence ||       !SameInstruction(retirement.instruction, head.instruction) ||       retirement.writes_register != head.writes_register ||       retirement.value != head.value || retirement.halt != head.halt) {

            throw std::runtime_error("retirement record does not equal queue head");

        }

        if (impl_->retired == std::numeric_limits<std::uint64_t>::max()) {

            throw std::runtime_error("counter overflow: retired");

        }

        if (retirement.writes_register &&       (retirement.instruction.rd <= 0 ||        retirement.instruction.rd >= kRegisterCount)) {

            throw std::runtime_error("retirement destination is invalid");

        }

        impl_->completion_next.pop_front();

        if (retirement.writes_register) {

            impl_->registers[retirement.instruction.rd] = retirement.value;

        }

        impl_->registers[0] = 0;

        if (retirement.halt) impl_->halted = true;

        ++impl_->retired;

        impl_->retirement_planned = true;

    }

    bool Machine::IsHalted() const {

        return impl_->halted;

    }

    void Machine::RecordCpuStall(CpuStallReason reason) {

        std::size_t point = 0;

        std::uint64_t* specific = nullptr;

        bool legacy_data = false;

        bool legacy_control = false;

        switch (reason) {

            case CpuStallReason::kControlDisabled:       point = 0;

            legacy_control = true;

            break;

            case CpuStallReason::kFetchQueueFull:       point = 0;

            specific = &impl_->fetch_queue_full_stalls;

            break;

            case CpuStallReason::kIcacheInput:       point = 0;

            specific = &impl_->icache_input_stalls;

            break;

            case CpuStallReason::kCompletionQueueFull:       point = 1;

            specific = &impl_->completion_queue_full_stalls;

            legacy_data = true;

            break;

            case CpuStallReason::kOutstandingRaw:       point = 1;

            specific = &impl_->outstanding_raw_stalls;

            legacy_data = true;

            break;

            case CpuStallReason::kOutstandingWaw:       point = 1;

            specific = &impl_->outstanding_waw_stalls;

            legacy_data = true;

            break;

            case CpuStallReason::kExecutionUnitBusy:       point = 1;

            specific = &impl_->execution_unit_busy_stalls;

            legacy_data = true;

            break;

            case CpuStallReason::kExecutionCompletionSlot:       point = 1;

            specific = &impl_->execution_completion_slot_stalls;

            legacy_data = true;

            break;

            case CpuStallReason::kPendingDataFull:       point = 2;

            specific = &impl_->pending_data_full_stalls;

            legacy_data = true;

            break;

            case CpuStallReason::kDcacheInput:       point = 2;

            specific = &impl_->dcache_input_stalls;

            legacy_data = true;

            break;

        }

        if (impl_->stall_point_used[point]) {

            throw std::runtime_error("CPU issue point recorded two stalls in one cycle");

        }

        if ((specific != nullptr &&        *specific == std::numeric_limits<std::uint64_t>::max()) ||       (legacy_control &&        impl_->control_stalls == std::numeric_limits<std::uint64_t>::max()) ||       (legacy_data && !impl_->data_stall_recorded &&        impl_->data_stalls == std::numeric_limits<std::uint64_t>::max())) {

            throw std::runtime_error("CPU stall counter overflow");

        }

        if (specific != nullptr) ++*specific;

        if (legacy_control) ++impl_->control_stalls;

        if (legacy_data && !impl_->data_stall_recorded) {

            ++impl_->data_stalls;

            impl_->data_stall_recorded = true;

        }

        impl_->stall_point_used[point] = true;

    }

    std::uint64_t Machine::RetiredInstructions() const {

        return impl_->retired;

    }

    std::uint64_t Machine::DataStalls() const {

        return impl_->data_stalls;

    }

    std::uint64_t Machine::ControlStalls() const {

        return impl_->control_stalls;

    }

}

// namespace pipesim
