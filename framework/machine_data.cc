#include "framework/internal.h"

namespace pipesim {

    using namespace framework_internal;

    bool Machine::CompletionQueueHasCapacity() const {

        return impl_->completion_current.size() <          impl_->config.cpu.completion_queue_entries;

    }

    bool Machine::NormalCompletionSlotAvailable(     std::uint64_t result_boundary) const {

        const std::size_t next_index =       impl_->ReservationNextIndex(result_boundary);

        if (impl_->reservation_next[next_index]) return false;

        if (next_index + 1U < impl_->reservation_current.size()) {

            return !impl_->reservation_current[next_index + 1U];

        }

        return true;

    }

    bool Machine::TryAllocateCompletion(     std::uint64_t sequence, const Instruction& instruction,     bool writes_register,     std::optional<std::uint64_t> normal_result_boundary) {

        if (impl_->completion_current.size() >=       impl_->config.cpu.completion_queue_entries) {

            return false;

        }

        const bool expected_write =       OpcodeWritesRegister(instruction.opcode) && instruction.rd != 0;

        if (writes_register != expected_write) {

            throw std::runtime_error("completion register-write metadata mismatch");

        }

        if ((IsMemoryOpcode(instruction.opcode) && normal_result_boundary) ||       (IsFixedOpcode(instruction.opcode) && !normal_result_boundary) ||       instruction.opcode == Opcode::kNop) {

            throw std::runtime_error("completion result-owner kind mismatch");

        }

        std::size_t reservation_index = 0;

        if (normal_result_boundary) {

            if (!NormalCompletionSlotAvailable(*normal_result_boundary)) return false;

            reservation_index =         impl_->ReservationNextIndex(*normal_result_boundary);

        }

        if (impl_->FindCompletion(&impl_->completion_next, sequence) != nullptr) {

            throw std::runtime_error("duplicate completion sequence");

        }

        if (!impl_->completion_next.empty() &&       impl_->completion_next.back().sequence >= sequence) {

            throw std::runtime_error("completion queue allocation is not in order");

        }

        Impl::Completion entry;

        entry.sequence = sequence;

        entry.instruction = instruction;

        entry.writes_register = writes_register;

        entry.normal_result_boundary = normal_result_boundary;

        impl_->completion_next.push_back(entry);

        if (normal_result_boundary) {

            impl_->reservation_next[reservation_index] = sequence;

        }

        return true;

    }

    bool Machine::HasOlderProducer(std::uint64_t sequence,                                int source_register) const {

        if (source_register < 0 || source_register >= kRegisterCount) {

            throw std::runtime_error("source register is out of range");

        }

        if (source_register == 0) return false;

        for (const Impl::Completion& entry : impl_->completion_next) {

            if (entry.sequence >= sequence) break;

            if (entry.writes_register && entry.instruction.rd == source_register) {

                return true;

            }

        }

        return false;

    }

    bool Machine::HasOlderDestination(std::uint64_t sequence,                                   int destination_register) const {

        if (destination_register == 0) return false;

        if (destination_register < 0 || destination_register >= kRegisterCount) {

            throw std::runtime_error("destination register is out of range");

        }

        for (const Impl::Completion& entry : impl_->completion_next) {

            if (entry.sequence >= sequence) break;

            if (entry.writes_register &&         entry.instruction.rd == destination_register) {

                return true;

            }

        }

        return false;

    }

    bool Machine::MemorySequenceMayIssue(std::uint64_t sequence) const {

        if(!impl_->external_submit) return true;

        auto* c=impl_->FindCompletion(impl_->completion_current,sequence);

        if(!c) return false;

        if(c->instruction.opcode==Opcode::kSw || c->instruction.opcode==Opcode::kXchg || c->instruction.opcode==Opcode::kFence)    return !impl_->completion_current.empty() && impl_->completion_current.front().sequence==sequence;

        return true;

    }

    bool Machine::PendingDataHasCapacity() const {

        if(impl_->external_submit) {

            for(const auto& p:impl_->data_current) if(p.valid) return false;

            return true;

        }

        return std::any_of(impl_->data_current.begin(), impl_->data_current.end(),                      [](const Impl::PendingData& entry) {

            return !entry.valid;
        });

    }

    bool Machine::DataRequestLinkCanAccept() const {

        if(impl_->external_submit) return PendingDataHasCapacity();

        return impl_->cpu_l1d_request.CanEnqueue();

    }

    bool Machine::TryIssueData(std::uint64_t sequence, std::uint32_t address,                            bool write, std::uint32_t write_value) {

        if (impl_->issued_data_this_cycle) {

            throw std::runtime_error("CPU attempted two data issues in one cycle");

        }

        if (address % 4 != 0 || static_cast<std::uint64_t>(address) + 4 >                                 impl_->config.data_memory.image_bytes) {

            throw std::runtime_error("CPU data address is unaligned or out of image");

        }

        Impl::PendingData* free_slot = nullptr;

        for (std::size_t i = 0; i < impl_->data_current.size(); ++i) {

            if (!impl_->data_current[i].valid) {

                free_slot = &impl_->data_next[i];

                break;

            }

        }

        if (free_slot == nullptr) {

            return false;

        }

        if(impl_->external_submit) {

            const auto* owner=impl_->FindCompletion(impl_->completion_current,sequence);

            if(!owner || owner->ready) throw std::runtime_error("external request without completion");

            // Prevent more than one active external memory operation per core.
            for(const auto& p:impl_->data_current) if(p.valid) return false;

            if(!impl_->external_submit(sequence,owner->instruction.opcode,address,write_value)) return false;

            free_slot->valid=true;

            free_slot->request_id=sequence;

            free_slot->sequence=sequence;

            free_slot->write=write;

            free_slot->address=address;

            impl_->issued_data_this_cycle=true;

            return true;

        }

        if (!impl_->cpu_l1d_request.CanEnqueue()) {

            return false;

        }

        const Impl::Completion* current_completion =       impl_->FindCompletion(impl_->completion_current, sequence);

        Impl::Completion* completion = impl_->FindCompletion(       &impl_->completion_next, sequence);

        const Opcode expected = write ? Opcode::kSw : Opcode::kLw;

        if (current_completion == nullptr || current_completion->ready ||       completion == nullptr || completion->ready ||       current_completion->instruction.opcode != expected ||       current_completion->normal_result_boundary) {

            throw std::runtime_error("data request lacks an incomplete completion entry");

        }

        const std::uint64_t id = impl_->transaction_ids.Reserve(       cache::TransactionSource::kCpuData);

        cache::CacheRequest request;

        request.id = id;

        request.origin_id = id;

        request.sequence = sequence;

        request.generation = 0;

        request.address = address;

        request.size = 4;

        request.type = write ? cache::RequestType::kWrite : cache::RequestType::kRead;

        if (write) request.data = cache::WordToBytes(write_value);

        cache::ValidateRequestPayload(request);

        impl_->cpu_l1d_request.PlanEnqueue(request);

        free_slot->valid = true;

        free_slot->request_id = id;

        free_slot->sequence = sequence;

        free_slot->write = write;

        free_slot->address = address;

        impl_->issued_data_this_cycle = true;

        return true;

    }

    void Machine::CompleteNonMemory(std::uint64_t sequence, std::uint32_t value,                                 bool halt) {

        if (impl_->normal_completion_seen || impl_->reservation_current.empty() ||       !impl_->reservation_current[0] ||       *impl_->reservation_current[0] != sequence) {

            throw std::runtime_error("normal completion port owner mismatch");

        }

        const Impl::Completion* current =       impl_->FindCompletion(impl_->completion_current, sequence);

        Impl::Completion* completion = impl_->FindCompletion(       &impl_->completion_next, sequence);

        if (current == nullptr || current->ready || completion == nullptr ||       completion->ready || !current->normal_result_boundary ||       *current->normal_result_boundary != impl_->current_boundary + 1U ||       !IsFixedOpcode(current->instruction.opcode) ||       halt != (current->instruction.opcode == Opcode::kHalt)) {

            throw std::runtime_error("invalid non-memory completion");

        }

        completion->value = value;

        completion->halt = halt;

        completion->ready = true;

        impl_->normal_completion_seen = true;

    }

}

// namespace pipesim
