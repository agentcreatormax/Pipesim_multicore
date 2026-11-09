#include "framework/internal.h"

namespace pipesim {

    using namespace framework_internal;

    std::uint32_t Machine::ReadRegister(int index) const {

        if (index < 0 || index >= kRegisterCount) {

            throw std::runtime_error("register index is out of range");

        }

        if (index == 0) return 0;

        return impl_->registers[index];

    }

    std::uint32_t Machine::EntryPoint() const {

        return impl_->entry_point;

    }

    bool Machine::FetchQueueHasCapacity() const {

        return std::any_of(impl_->fetch_current.begin(), impl_->fetch_current.end(),                      [](const Impl::FetchSlot& slot) {

            return slot.state == Impl::FetchState::kFree;
        });

    }

    bool Machine::FetchRequestLinkCanAccept() const {

        return impl_->cpu_l1i_request.CanEnqueue();

    }

    bool Machine::TryIssueFetch(std::uint32_t pc) {

        BranchPredictionSet predictions;

        predictions.next_pc = pc + 4;

        return TryIssuePredictedFetch(pc, predictions, 0);

    }

    bool Machine::TryIssuePredictedFetch(     std::uint32_t pc,     const BranchPredictionSet& predictions,     std::uint64_t cycle) {

        if (impl_->issued_fetch_this_cycle) {

            throw std::runtime_error("CPU attempted two fetch issues in one cycle");

        }

        if (pc % 4 != 0 || static_cast<std::uint64_t>(pc) + 4 >                            impl_->config.instruction_memory.image_bytes) {

            throw std::runtime_error(         "CPU fetch address is unaligned or out of image");

        }

        Impl::FetchSlot* free_slot = nullptr;

        for (std::size_t i = 0; i < impl_->fetch_current.size(); ++i) {

            if (impl_->fetch_current[i].state == Impl::FetchState::kFree) {

                free_slot = &impl_->fetch_next[i];

                break;

            }

        }

        if (free_slot == nullptr) {

            return false;

        }

        if (!impl_->cpu_l1i_request.CanEnqueue()) {

            return false;

        }

        if (impl_->next_fetch_sequence_next == cache::kNoSequence) {

            throw std::runtime_error("CPU fetch sequence space exhausted");

        }

        const std::uint64_t id = impl_->transaction_ids.Reserve(       cache::TransactionSource::kCpuInstruction);

        const std::uint64_t sequence = impl_->next_fetch_sequence_next++;

        cache::CacheRequest request;

        request.id = id;

        request.origin_id = id;

        request.sequence = sequence;

        request.generation = impl_->fetch_generation_next;

        request.address = pc;

        request.size = 4;

        request.type = cache::RequestType::kRead;

        cache::ValidateRequestPayload(request);

        impl_->cpu_l1i_request.PlanEnqueue(request);

        free_slot->state = Impl::FetchState::kPending;

        free_slot->request_id = id;

        free_slot->sequence = sequence;

        free_slot->generation = impl_->fetch_generation_next;

        free_slot->address = pc;

        free_slot->fetch_cycle = cycle;

        free_slot->predictions = predictions;

        impl_->issued_fetch_this_cycle = true;

        return true;

    }

    bool Machine::PeekFetched(FetchedInstruction* fetched) const {

        const Impl::FetchSlot* oldest = nullptr;

        for (const Impl::FetchSlot& slot : impl_->fetch_current) {

            if ((slot.state != Impl::FetchState::kPending &&          slot.state != Impl::FetchState::kReady) ||         slot.generation != impl_->fetch_generation_current) {

                continue;

            }

            if (oldest == nullptr || slot.sequence < oldest->sequence) oldest = &slot;

        }

        if (oldest == nullptr || oldest->state != Impl::FetchState::kReady) {

            return false;

        }

        const auto instruction = impl_->token_to_instruction.find(oldest->token);

        if (instruction == impl_->token_to_instruction.end() ||       instruction->second.pc != oldest->address) {

            throw std::runtime_error("fetched word is not an instruction token for its address");

        }

        if (fetched != nullptr) {

            fetched->sequence = oldest->sequence;

            fetched->generation = oldest->generation;

            fetched->fetch_cycle = oldest->fetch_cycle;

            fetched->predictions = oldest->predictions;

            fetched->instruction = instruction->second;

        }

        return true;

    }

    void Machine::ConsumeFetched() {

        const Impl::FetchSlot* oldest = nullptr;

        for (const Impl::FetchSlot& slot : impl_->fetch_current) {

            if ((slot.state != Impl::FetchState::kPending &&          slot.state != Impl::FetchState::kReady) ||         slot.generation != impl_->fetch_generation_current) {

                continue;

            }

            if (oldest == nullptr || slot.sequence < oldest->sequence) oldest = &slot;

        }

        if (oldest == nullptr || oldest->state != Impl::FetchState::kReady) {

            throw std::runtime_error("ConsumeFetched called without a ready head");

        }

        Impl::FetchSlot* next = impl_->FindFetch(&impl_->fetch_next,                                           oldest->request_id);

        if (next == nullptr) throw std::runtime_error("ready fetch disappeared");

        *next = Impl::FetchSlot();

    }

    void Machine::InvalidateFetchesAfter(std::uint64_t sequence) {

        if (impl_->fetch_generation_next == std::numeric_limits<std::uint32_t>::max()) {

            throw std::runtime_error("fetch generation space exhausted");

        }

        const std::uint32_t old_generation = impl_->fetch_generation_next;

        std::uint64_t ready_discards = 0;

        for (const Impl::FetchSlot& slot : impl_->fetch_next) {

            if (slot.state == Impl::FetchState::kReady &&         slot.generation == old_generation && slot.sequence > sequence) {

                ++ready_discards;

            }

        }

        if (ready_discards >       std::numeric_limits<std::uint64_t>::max() -           impl_->stale_ifetch_responses) {

            throw std::runtime_error("counter overflow: stale_ifetch_responses");

        }

        ++impl_->fetch_generation_next;

        for (Impl::FetchSlot& slot : impl_->fetch_next) {

            if (slot.state == Impl::FetchState::kFree ||         slot.generation != old_generation || slot.sequence <= sequence) {

                continue;

            }

            if (slot.state == Impl::FetchState::kPending) {

                slot.state = Impl::FetchState::kStalePending;

            }

            else {

                slot = Impl::FetchSlot();

                ++impl_->stale_ifetch_responses;

            }

        }

    }

    SquashReport Machine::SquashAfter(std::uint64_t sequence) {

        SquashReport report;

        for (const Impl::FetchSlot& slot : impl_->fetch_next) {

            if (slot.state != Impl::FetchState::kFree && slot.sequence > sequence) {

                ++report.fetches;

            }

        }

        for (const Impl::PendingData& request : impl_->data_next) {

            if (request.valid && request.sequence > sequence) {

                ++report.data_requests;

            }

        }

        if (report.data_requests != 0) {

            throw std::runtime_error("a speculative data request reached the cache");

        }

        InvalidateFetchesAfter(sequence);

        auto first_younger_completion = std::find_if(       impl_->completion_next.begin(), impl_->completion_next.end(),       [sequence](const Impl::Completion& completion) {

            return completion.sequence > sequence;
        });

        report.completions = static_cast<std::uint64_t>(       std::distance(first_younger_completion, impl_->completion_next.end()));

        impl_->completion_next.erase(first_younger_completion,                                impl_->completion_next.end());

        for (auto& reservation : impl_->reservation_next) {

            if (reservation && *reservation > sequence) reservation.reset();

        }

        return report;

    }

    const cache::SystemConfig& Machine::Configuration() const {

        return impl_->config;

    }

    cache::TransactionIdService* Machine::TransactionIds() {

        return &impl_->transaction_ids;

    }

}

// namespace pipesim
