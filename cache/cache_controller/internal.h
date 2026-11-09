#pragma once
#include "cache/cache_controller.h"
#include "cache/replacement.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace pipesim::cache
{
  namespace cache_controller_internal
  {
    enum class OwnerState
    {
      kUnsent,
      kInFlight,
      kResponding,
      kAckReady
    };
    struct AddressFields
    {
      std::uint32_t block = 0, set = 0, tag = 0, offset = 0;
    };
    struct PipelineEntry
    {
      bool valid = false;
      CacheRequest request;
      std::uint64_t arrival_order = 0;
      AddressFields address;
    };
    struct OutputEntry
    {
      bool valid = false;
      CacheResponse response;
      std::uint64_t arrival_order = 0;
    };
    struct MshrEntry
    {
      bool valid = false;
      std::uint32_t block_address = 0, set = 0, tag = 0, reserved_way = 0;
      std::uint64_t arrival_order = 0;
      OwnerState state = OwnerState::kUnsent;
      CacheRequest lower_request;
      std::vector<MshrWaiter> waiters;
    };
    struct WritebackEntry
    {
      bool valid = false;
      std::uint32_t address = 0, size = 0;
      std::vector<std::uint8_t> data;
      std::uint64_t arrival_order = 0;
      OwnerState state = OwnerState::kUnsent;
      CacheRequest lower_request;
      std::vector<WritebackAckWaiter> waiters;
      std::uint32_t demand_bypasses = 0;
    };
    enum class ResponseOwner
    {
      kNone,
      kOutput,
      kMshr,
      kWriteback
    };
    struct ResponseChoice
    {
      ResponseOwner owner = ResponseOwner::kNone;
      std::size_t entry = 0, waiter = 0;
      CacheResponse response;
      std::uint64_t arrival_order = 0;
    };
    enum class RequestOwner
    {
      kNone,
      kMshr,
      kWriteback
    };
    struct RequestChoice
    {
      RequestOwner owner = RequestOwner::kNone;
      std::size_t entry = 0;
      CacheRequest request;
      std::uint64_t arrival_order = 0;
      bool demand_blocked_by_writeback = false;
      std::optional<std::size_t> oldest_eligible_writeback;
    };
    enum class LookupKind
    {
      kEmpty,
      kHit,
      kMshrMerge,
      kMshrAllocate,
      kWritebackForward,
      kHold
    };
    enum class LookupStall
    {
      kNone,
      kOutput,
      kMshrWaiterFull,
      kMshrFull,
      kReservedWay,
      kWritebackOrder,
      kWritebackWaiterFull,
      kWritebackFull
    };
    struct LookupDecision
    {
      LookupKind kind = LookupKind::kEmpty;
      LookupStall stall = LookupStall::kNone;
      int hit_way = -1, mshr = -1, victim_way = -1;
    };
    struct WritebackPlan
    {
      bool allowed = false;
      std::vector<std::size_t> merge_entries;
      int survivor = -1;
      LookupStall stall = LookupStall::kNone;
    };
    inline bool RangesOverlap(std::uint32_t a, std::uint32_t as, std::uint32_t b, std::uint32_t bs)
    {
      const auto ae = static_cast<std::uint64_t>(a) + as, be = static_cast<std::uint64_t>(b) + bs;
      return a < be && b < ae;
    }
    inline bool ResponseLess(const ResponseChoice &a, const ResponseChoice &b) { return std::tie(a.arrival_order, a.response.id) < std::tie(b.arrival_order, b.response.id); }
    inline TransactionSource SourceForRole(CacheRole role)
    {
      switch (role)
      {
      case CacheRole::kL1I:
        return TransactionSource::kL1I;
      case CacheRole::kL1D:
        return TransactionSource::kL1D;
      case CacheRole::kL2I:
        return TransactionSource::kL2I;
      case CacheRole::kL2D:
        return TransactionSource::kL2D;
      }
      throw std::runtime_error("invalid cache role");
    }
    inline const char *OwnerStateName(OwnerState state)
    {
      switch (state)
      {
      case OwnerState::kUnsent:
        return "UNSENT";
      case OwnerState::kInFlight:
        return "IN_FLIGHT";
      case OwnerState::kResponding:
        return "RESPONDING";
      case OwnerState::kAckReady:
        return "ACK_READY";
      }
      return "UNKNOWN";
    }
  }
  using namespace cache_controller_internal;

  class CacheController::Impl
  {
  public:
    Impl(CacheRole role, const CacheConfig &config,
         TransactionIdService *transaction_ids)
        : role_(role),
          config_(config),
          transaction_ids_(transaction_ids),
          current_lines_(config.sets,
                         std::vector<CacheLine>(config.ways)),
          next_lines_(current_lines_),
          current_mshrs_(config.mshr_entries),
          next_mshrs_(current_mshrs_),
          current_writebacks_(config.writeback_entries),
          next_writebacks_(current_writebacks_),
          replacement_(MakeReplacementPolicy(config.replacement, config.sets,
                                             config.ways))
    {
      if (transaction_ids_ == nullptr)
      {
        throw std::runtime_error("cache controller transaction-id service is null");
      }
      Reset();
    }

    void Reset()
    {
      for (auto &set : current_lines_)
      {
        for (CacheLine &line : set)
        {
          line = CacheLine();
          line.data.assign(config_.line_size, 0);
        }
      }
      next_lines_ = current_lines_;
      current_index_ = PipelineEntry();
      current_lookup_ = PipelineEntry();
      current_output_ = OutputEntry();
      next_index_ = current_index_;
      next_lookup_ = current_lookup_;
      next_output_ = current_output_;
      current_mshrs_.assign(config_.mshr_entries, MshrEntry());
      next_mshrs_ = current_mshrs_;
      current_writebacks_.assign(config_.writeback_entries, WritebackEntry());
      next_writebacks_ = current_writebacks_;
      current_arrival_ = 0;
      next_arrival_ = 0;
      stats_ = CacheStats();
      next_stats_ = stats_;
      replacement_->Reset();
      begun_ = false;
      operated_ = false;
    }

    void BeginCycle()
    {
      if (begun_)
      {
        throw std::runtime_error("cache BeginCycle called twice");
      }
      next_lines_ = current_lines_;
      next_index_ = current_index_;
      next_lookup_ = current_lookup_;
      next_output_ = current_output_;
      next_mshrs_ = current_mshrs_;
      next_writebacks_ = current_writebacks_;
      next_arrival_ = current_arrival_;
      next_stats_ = stats_;
      replacement_->BeginCycle();
      begun_ = true;
      operated_ = false;
    }

    // local address mapping using this controller's
    // line_size and sets. Return block base, set, tag, offset. See SPEC.md/
    // ABI.md for the exact formula.
    AddressFields MapAddress(std::uint32_t address) const
    {
      (void)address;
      // start

      AddressFields fields;
      fields.offset = address & (config_.line_size - 1);
      fields.set = (address / config_.line_size) % config_.sets;
      fields.tag = address / (config_.line_size * config_.sets);
      fields.block = address / config_.line_size; // block number
      return fields;

      // end
    }

    void ValidateInbound(const CacheRequest &request) const
    {
      ValidateRequestPayload(request);
      if (!RangeFitsLine(request.address, request.size, config_.line_size))
      {
        throw std::runtime_error(std::string(CacheRoleName(role_)) +
                                 " request crosses a local cache line");
      }
      switch (role_)
      {
      case CacheRole::kL1I:
      case CacheRole::kL2I:
        if (request.type != RequestType::kRead)
        {
          throw std::runtime_error("instruction cache received a write request");
        }
        break;
      case CacheRole::kL1D:
        if (request.type != RequestType::kRead &&
            request.type != RequestType::kWrite)
        {
          throw std::runtime_error("L1D received an illegal request type");
        }
        break;
      case CacheRole::kL2D:
        if (request.type != RequestType::kRead &&
            request.type != RequestType::kWriteback)
        {
          throw std::runtime_error("L2D received an illegal request type");
        }
        break;
      }
      if ((role_ == CacheRole::kL1I || role_ == CacheRole::kL1D) &&
          request.origin_id != request.id)
      {
        throw std::runtime_error("L1 CPU request origin must equal link id");
      }
      if ((role_ == CacheRole::kL1I || role_ == CacheRole::kL1D) &&
          (request.size != 4 || request.address % 4 != 0))
      {
        throw std::runtime_error("CPU cache request must be one aligned word");
      }
      if ((role_ == CacheRole::kL1D || role_ == CacheRole::kL2D) &&
          request.type != RequestType::kWriteback && request.generation != 0)
      {
        throw std::runtime_error("data demand carries a fetch generation");
      }
    }

    int FindHit(const AddressFields &address) const
    {
      int hit = -1;
      for (std::uint32_t way = 0; way < config_.ways; ++way)
      {
        const CacheLine &line = current_lines_[address.set][way];
        if (line.valid && line.tag == address.tag)
        {
          if (hit >= 0)
            throw std::runtime_error("duplicate tags in one cache set");
          hit = static_cast<int>(way);
        }
      }
      return hit;
    }

    int FindMshrByBlock(std::uint32_t block) const
    {
      int found = -1;
      for (std::size_t i = 0; i < current_mshrs_.size(); ++i)
      {
        if (current_mshrs_[i].valid &&
            current_mshrs_[i].block_address == block)
        {
          if (found >= 0)
            throw std::runtime_error("duplicate MSHR block owner");
          found = static_cast<int>(i);
        }
      }
      return found;
    }

    int FindMshrByLowerId(std::uint64_t id) const
    {
      int found = -1;
      for (std::size_t i = 0; i < current_mshrs_.size(); ++i)
      {
        if (current_mshrs_[i].valid &&
            current_mshrs_[i].lower_request.id == id)
        {
          if (found >= 0)
            throw std::runtime_error("duplicate MSHR lower id");
          found = static_cast<int>(i);
        }
      }
      return found;
    }

    int FindWritebackByLowerId(std::uint64_t id) const
    {
      int found = -1;
      for (std::size_t i = 0; i < current_writebacks_.size(); ++i)
      {
        if (current_writebacks_[i].valid &&
            current_writebacks_[i].lower_request.id == id)
        {
          if (found >= 0)
            throw std::runtime_error("duplicate writeback lower id");
          found = static_cast<int>(i);
        }
      }
      return found;
    }

    int FindFreeMshr() const
    {
      for (std::size_t i = 0; i < current_mshrs_.size(); ++i)
      {
        if (!current_mshrs_[i].valid)
          return static_cast<int>(i);
      }
      return -1;
    }

    int FindFreeWriteback() const
    {
      for (std::size_t i = 0; i < current_writebacks_.size(); ++i)
      {
        if (!current_writebacks_[i].valid)
          return static_cast<int>(i);
      }
      return -1;
    }

    std::vector<bool> EligibleWays(std::uint32_t set) const
    {
      std::vector<bool> eligible(config_.ways, false);
      for (std::uint32_t way = 0; way < config_.ways; ++way)
      {
        eligible[way] = !current_lines_[set][way].reserved;
      }
      return eligible;
    }

    int PeekVictim(std::uint32_t set) const
    {
      const std::vector<bool> eligible = EligibleWays(set);
      for (std::uint32_t way = 0; way < config_.ways; ++way)
      {
        if (eligible[way] && !current_lines_[set][way].valid)
          return way;
      }
      if (std::none_of(eligible.begin(), eligible.end(),
                       [](bool value) { return value; }))
      {
        return -1;
      }
      return replacement_->PeekVictim(set, eligible);
    }

    // choose/reserve a deterministic victim among
    // currently-eligible, currently-invalid-first ways; fall back to the
    // configured replacement policy. See SPEC.md's three-step victim
    // priority.
    int SelectVictim(std::uint32_t set)
    {
      (void)set;
      // start

      if (set >= config_.sets)
      {
        throw std::runtime_error("SelectVictim: invalid set");
      }
      const std::vector<bool> eligible = EligibleWays(set);

      // eligible lowest-index invalid way.
      for (std::uint32_t way = 0; way < config_.ways; ++way)
      {
        if (eligible[way] && !current_lines_[set][way].valid)
        {
          next_lines_[set][way].reserved = true;
          return static_cast<int>(way);
        }
      }

      // replacement
      if (std::none_of(eligible.begin(), eligible.end(),
                       [](bool value) { return value; }))
      {
        return -1;
      }
      const int victim = replacement_->SelectVictim(set, eligible);
      if (victim < 0 ||
          static_cast<std::uint32_t>(victim) >= config_.ways ||
          !eligible[static_cast<std::uint32_t>(victim)])
      {
        throw std::runtime_error("SelectVictim: replacement policy returned invalid victim");
      }

      next_lines_[set][static_cast<std::uint32_t>(victim)].reserved = true;
      return victim;

      // end
    }

    // compute the transitive UNSENT overlap closure for a
    // prospective writeback of [address, address+size); hold
    // (kWritebackOrder) on any IN_FLIGHT or same-cycle-granted overlap;
    // enforce the bounded combined waiter count; choose the oldest survivor
    // by (arrival_order, id).
    WritebackPlan PlanWriteback(std::uint32_t address, std::uint32_t size,
                                std::size_t new_waiters,
                                int granted_writeback = -1) const
    {
      (void)address;
      (void)size;
      (void)new_waiters;
      (void)granted_writeback;
      // start

      WritebackPlan plan;
      if (size == 0)
      {
        plan.stall = LookupStall::kWritebackOrder;
        return plan;
      }

      // transitive overlap group of UNSENT writeback
      std::vector<std::size_t> group;

      // overlap check with existing groups
      auto overlaps_group = [&](const WritebackEntry &wb)
      {
        if (RangesOverlap(address, size, wb.address, wb.size))
        {
          return true;
        }
        for (std::size_t index : group)
        {
          const WritebackEntry &member = current_writebacks_[index];
          if (RangesOverlap(wb.address, wb.size, member.address, member.size))
          {
            return true;
          }
        }
        return false;
      };

      //  cannot merge overlapping IN_FLIGHT writeback
      for (std::size_t i = 0; i < current_writebacks_.size(); ++i)
      {
        const WritebackEntry &wb = current_writebacks_[i];
        if (!wb.valid)
        {
          continue;
        }
        if (wb.state == OwnerState::kInFlight &&
            RangesOverlap(address, size, wb.address, wb.size))
        {
          plan.stall = LookupStall::kWritebackOrder;
          return plan;
        }
        if (static_cast<int>(i) == granted_writeback &&
            RangesOverlap(address, size, wb.address, wb.size))
        {
          plan.stall = LookupStall::kWritebackOrder;
          return plan;
        }
      }

      bool changed = true;
      while (changed)
      {
        changed = false;
        for (std::size_t i = 0; i < current_writebacks_.size(); ++i)
        {
          const WritebackEntry &wb = current_writebacks_[i];
          if (!wb.valid || wb.state != OwnerState::kUnsent)
          {
            continue;
          }
          if (std::find(group.begin(), group.end(), i) != group.end())
          {
            continue;
          }
          if (overlaps_group(wb))
          {
            group.push_back(i);
            changed = true;
          }
        }
      }

      // Calculate the total number of acknowledgement waiters after merging.
      std::size_t total_waiters = new_waiters;
      for (std::size_t index : group)
      {
        total_waiters += current_writebacks_[index].waiters.size();
      }
      if (total_waiters > config_.writeback_waiters_per_entry)
      {
        plan.stall = LookupStall::kWritebackWaiterFull;
        return plan;
      }

      // if no overlapping create new slot, but check for free slot first
      if (group.empty())
      {
        if (FindFreeWriteback() < 0)
        {
          plan.stall = LookupStall::kWritebackFull;
          return plan;
        }

        plan.allowed = true;
        plan.merge_entries.clear();
        plan.survivor = -1;
        plan.stall = LookupStall::kNone;
        return plan;
      }

      std::size_t survivor = group[0];

      for (std::size_t index : group)
      {
        const WritebackEntry &candidate = current_writebacks_[index];
        const WritebackEntry &current = current_writebacks_[survivor];

        if (std::tie(candidate.arrival_order, candidate.lower_request.id) <
            std::tie(current.arrival_order, current.lower_request.id))
        {
          survivor = index;
        }
      }

      plan.allowed = true;
      plan.merge_entries = group;
      plan.survivor = static_cast<int>(survivor);
      plan.stall = LookupStall::kNone;

      return plan;

      // end
    }

    // true iff a granted upper response this cycle would
    // drain current OUTPUT (used to plan same-cycle OUTPUT reuse).
    bool OutputWillDrain(bool response_granted) const
    {
      (void)response_granted;
      // start

      if (!response_granted || !current_output_.valid)
      {
        return false;
      }
      const ResponseChoice choice = SelectResponse();
      return choice.owner == ResponseOwner::kOutput;

      // end
    }

    // the parallel LOOKUP decision -- hit, MSHR
    // merge/allocate, writeback-forward, or hold with the specific stall
    // reason, in the priority SPEC.md/ABI.md define. Must not mutate state.
    // Existing helpers: FindHit, FindMshrByBlock, FindFreeMshr, PeekVictim, FindFreeWriteback, PlanWriteback.
    LookupDecision DecideLookup(bool output_space,
                                int granted_writeback = -1) const
    {
      (void)output_space;
      (void)granted_writeback;
      // start

      LookupDecision decision;

      // Nothing is currently in LOOKUP.
      if (!current_lookup_.valid)
      {
        decision.kind = LookupKind::kEmpty;
        decision.stall = LookupStall::kNone;
        return decision;
      }

      const PipelineEntry &pipeline = current_lookup_;
      const CacheRequest &request = pipeline.request;

      const int hit_way = FindHit(pipeline.address);

      if (hit_way >= 0)
      {
        if (!output_space)
        {
          decision.kind = LookupKind::kHold;
          decision.stall = LookupStall::kOutput;
          return decision;
        }
        decision.kind = LookupKind::kHit;
        decision.stall = LookupStall::kNone;
        decision.hit_way = hit_way;
        return decision;
      }

      // ------------------------------------------------------------
      // 2. A kWriteback that misses is forwarded instead of causing
      //    a normal cache allocation (no-write-allocate).
      // ------------------------------------------------------------
      if (request.type == RequestType::kWriteback)
      {
        const WritebackPlan plan =
            PlanWriteback(request.address, request.size,
                          /*new_waiters=*/1, granted_writeback);

        if (!plan.allowed)
        {
          decision.kind = LookupKind::kHold;
          decision.stall = plan.stall;
          return decision;
        }

        decision.kind = LookupKind::kWritebackForward;
        decision.stall = LookupStall::kNone;
        return decision;
      }

      // block already has an outstanding MSHR -> merge.
      const int existing_mshr =
          FindMshrByBlock(pipeline.address.block);

      if (existing_mshr >= 0)
      {
        const MshrEntry &mshr =
            current_mshrs_[static_cast<std::size_t>(existing_mshr)];

        if (mshr.waiters.size() >=
            static_cast<std::size_t>(config_.mshr_waiters_per_entry))
        {
          decision.kind = LookupKind::kHold;
          decision.stall = LookupStall::kMshrWaiterFull;
          return decision;
        }

        decision.kind = LookupKind::kMshrMerge;
        decision.stall = LookupStall::kNone;
        decision.mshr = existing_mshr;
        return decision;
      }

      // No existing MSHR and MSHR free check.
      const int free_mshr = FindFreeMshr();

      if (free_mshr < 0)
      {
        decision.kind = LookupKind::kHold;
        decision.stall = LookupStall::kMshrFull;
        return decision;
      }

      // We also need an eligible victim way.
      const int victim_way = PeekVictim(pipeline.address.set);

      if (victim_way < 0)
      {
        decision.kind = LookupKind::kHold;
        decision.stall = LookupStall::kReservedWay;
        return decision;
      }

      const CacheLine &victim =
          current_lines_[pipeline.address.set][victim_way];

      // If the victim is dirty, handle writeback.
      if (victim.valid && victim.dirty)
      {
        const std::uint32_t victim_block = victim.tag * config_.sets + pipeline.address.set;
        const std::uint32_t victim_address = victim_block * config_.line_size;
        const WritebackPlan plan = PlanWriteback(victim_address, config_.line_size, 0, granted_writeback);
        if (!plan.allowed)
        {
          decision.kind = LookupKind::kHold;
          decision.stall = plan.stall;
          return decision;
        }
      }

      decision.kind = LookupKind::kMshrAllocate;
      decision.stall = LookupStall::kNone;
      decision.mshr = free_mshr;
      decision.victim_way = victim_way;
      return decision;

      // end
    }

    // plan three-stage backpressure from current_* state
    // and the resolved grant flags only. accept_lower_response must be true
    // iff lower_response is a recognized, correctly-typed, echo-valid owner
    // (MSHR fill or writeback ack) currently IN_FLIGHT. An owned demand fill
    // takes the combined LOOKUP port for the cycle. accept_upper_request must
    // reflect whether INDEX is free or about to be vacated into LOOKUP this
    // cycle. Must not mutate state.
    // Existing helpers: FindMshrByLowerId, FindWritebackByLowerId, ValidateEcho, ValidateInbound, OutputWillDrain, DecideLookup.
    CacheStageIntent StageIntent(const CacheRequest *upper_request,
                                 const CacheResponse *lower_response,
                                 bool response_granted,
                                 bool lower_request_granted) const
    {
      (void)upper_request;
      (void)lower_response;
      (void)response_granted;
      (void)lower_request_granted;
      // start

      CacheStageIntent intent;

      // Check lower response
      bool lower_fill_accepted = false;

      if (lower_response != nullptr)
      {
        const int mshr =
            FindMshrByLowerId(lower_response->id);

        if (mshr >= 0)
        {
          const MshrEntry &entry =
              current_mshrs_[static_cast<std::size_t>(mshr)];

          if (entry.state == OwnerState::kInFlight)
          {
            ValidateEcho(
                *lower_response,
                entry.lower_request,
                ResponseType::kReadData);

            intent.accept_lower_response = true;
            lower_fill_accepted = true;
          }
        }
        else
        {
          const int writeback =
              FindWritebackByLowerId(lower_response->id);

          if (writeback >= 0)
          {
            const WritebackEntry &entry =
                current_writebacks_[static_cast<std::size_t>(writeback)];

            if (entry.state == OwnerState::kInFlight)
            {
              ValidateEcho(
                  *lower_response,
                  entry.lower_request,
                  ResponseType::kWritebackAck);

              intent.accept_lower_response = true;
            }
          }
        }
      }

      // upper-request acceptance
      const bool output_space = !current_output_.valid || OutputWillDrain(response_granted);

      // check cycle's lower request is a writeback.
      int granted_writeback = -1;
      if (lower_request_granted)
      {
        const RequestChoice choice = SelectLowerRequest();
        if (choice.owner == RequestOwner::kWriteback)
        {
          granted_writeback = static_cast<int>(choice.entry);
        }
      }

      // LOOKUP -> OUTPUT
      bool lookup_can_vacate = !current_lookup_.valid;

      if (current_lookup_.valid && !lower_fill_accepted)
      {
        const LookupDecision decision = DecideLookup(output_space, granted_writeback);
        lookup_can_vacate = decision.kind != LookupKind::kHold;
      }

      // INDEX -> LOOKUP
      const bool index_can_vacate = lookup_can_vacate;

      // upper request -> INDEX
      intent.accept_upper_request = !current_index_.valid || index_can_vacate;

      return intent;

      // end
    }

    // construct a waiter (initialize both response flags
    // false, response_data empty) that records identity/order enough to
    // reconstruct an upward CacheResponse later. See ABI.md's MshrWaiter
    // invariants.
    MshrWaiter MakeMshrWaiter(const PipelineEntry &pipeline) const
    {
      (void)pipeline;
      // start

      const CacheRequest &request = pipeline.request;
      MshrWaiter waiter;

      waiter.request_id = request.id;
      waiter.origin_id = request.origin_id;
      waiter.sequence = request.sequence;
      waiter.arrival_order = pipeline.arrival_order;
      waiter.generation = request.generation;
      waiter.type = request.type;
      waiter.address = request.address;
      waiter.size = request.size;
      waiter.data = request.data;

      waiter.response_ready = false;
      waiter.response_sent = false;
      waiter.response_data.clear();

      return waiter;

      // end
    }

    
    WritebackAckWaiter MakeWritebackWaiter(
        const PipelineEntry &pipeline) const
    {
      (void)pipeline;
      // start

      const CacheRequest &request = pipeline.request;
      if (request.type != RequestType::kWriteback)
      {
        throw std::runtime_error("MakeWritebackWaiter called for non-writeback request");
      }

      WritebackAckWaiter waiter;
      waiter.request_id = request.id;
      waiter.origin_id = request.origin_id;
      waiter.sequence = request.sequence;
      waiter.arrival_order = pipeline.arrival_order;
      waiter.generation = request.generation;
      waiter.address = request.address;
      waiter.size = request.size;
      waiter.response_sent = false;
      return waiter;

      // end
    }

    std::uint64_t AllocateArrival()
    {
      if (next_arrival_ == std::numeric_limits<std::uint64_t>::max())
      {
        throw std::runtime_error("cache arrival-order counter overflow");
      }
      return next_arrival_++;
    }

    // construct the response for a committed hit
    // (kReadData with the read bytes, kWriteAck, or kWritebackAck).
    CacheResponse ResponseForHit(const PipelineEntry &pipeline,
                                 const CacheLine &line) const
    {
      (void)pipeline;
      (void)line;
      // start

      CacheResponse response;
      response.id = pipeline.request.id;
      response.origin_id = pipeline.request.origin_id;
      response.sequence = pipeline.request.sequence;
      response.generation = pipeline.request.generation;
      response.address = pipeline.request.address;
      response.size = pipeline.request.size;
      // read hit -> extract bytes
      if (pipeline.request.type == RequestType::kRead)
      {
        response.type = ResponseType::kReadData;
        response.data.assign(
            line.data.begin() + pipeline.address.offset,
            line.data.begin() + pipeline.address.offset + pipeline.request.size);
      }
      // write hit -> send ack)
      else if (pipeline.request.type == RequestType::kWrite)
      {
        response.type = ResponseType::kWriteAck;
        response.data.clear();
      }
      // writeback hit -> send ack
      else if (pipeline.request.type == RequestType::kWriteback)
      {
        response.type = ResponseType::kWritebackAck;
        response.data.clear();
      }
      else
      {
        throw std::runtime_error("illegal request type for hit response");
      }

      return response;

      // end
    }

    // consider OUTPUT, the oldest ready-unsent waiter of
    // each responding MSHR, and the oldest unsent waiter of each ACK_READY
    // writeback; select the minimum (arrival_order, response id). Must not
    // mutate state.
    ResponseChoice SelectResponse() const
    {
      // start

      ResponseChoice best;
      bool have_best = false;

      // output
      if (current_output_.valid)
      {
        ResponseChoice candidate;
        candidate.owner = ResponseOwner::kOutput;
        candidate.entry = 0;
        candidate.waiter = 0;
        candidate.response = current_output_.response;
        candidate.arrival_order = current_output_.arrival_order;

        best = candidate;
        have_best = true;
      }

      // mshr oldest unsent ready waiter
      for (std::size_t i = 0; i < current_mshrs_.size(); ++i)
      {
        const MshrEntry &mshr = current_mshrs_[i];

        if (!mshr.valid || mshr.state != OwnerState::kResponding)
        {
          continue;
        }

        int oldest_waiter = -1;

        for (std::size_t w = 0; w < mshr.waiters.size(); ++w)
        {
          const MshrWaiter &waiter = mshr.waiters[w];

          if (!waiter.response_ready || waiter.response_sent)
          {
            continue;
          }

          if (oldest_waiter < 0 ||
              std::tie(waiter.arrival_order, waiter.request_id) <
                  std::tie(mshr.waiters[oldest_waiter].arrival_order,
                           mshr.waiters[oldest_waiter].request_id))
          {
            oldest_waiter = static_cast<int>(w);
          }
        }

        if (oldest_waiter < 0)
        {
          continue;
        }

        const MshrWaiter &waiter = mshr.waiters[oldest_waiter];

        ResponseChoice candidate;
        candidate.owner = ResponseOwner::kMshr;
        candidate.entry = i;
        candidate.waiter = static_cast<std::size_t>(oldest_waiter);
        candidate.arrival_order = waiter.arrival_order;

        candidate.response.id = waiter.request_id;
        candidate.response.origin_id = waiter.origin_id;
        candidate.response.sequence = waiter.sequence;
        candidate.response.generation = waiter.generation;
        candidate.response.address = waiter.address;
        candidate.response.size = waiter.size;

        if (waiter.type == RequestType::kRead)
        {
          candidate.response.type = ResponseType::kReadData;
          candidate.response.data = waiter.response_data;
        }
        else if (waiter.type == RequestType::kWrite)
        {
          candidate.response.type = ResponseType::kWriteAck;
          candidate.response.data.clear();
        }
        else
        {
          throw std::runtime_error(
              "MSHR waiter has illegal request type");
        }

        if (!have_best || ResponseLess(candidate, best))
        {
          best = candidate;
          have_best = true;
        }
      }

      // oldest unsent ACK_READY writeback waiter
      for (std::size_t i = 0; i < current_writebacks_.size(); ++i)
      {
        const WritebackEntry &writeback = current_writebacks_[i];

        if (!writeback.valid || writeback.state != OwnerState::kAckReady)
        {
          continue;
        }

        int oldest_waiter = -1;

        for (std::size_t w = 0; w < writeback.waiters.size(); ++w)
        {
          const WritebackAckWaiter &waiter = writeback.waiters[w];

          if (waiter.response_sent)
          {
            continue;
          }

          if (oldest_waiter < 0 ||
              std::tie(waiter.arrival_order, waiter.request_id) <
                  std::tie(writeback.waiters[oldest_waiter].arrival_order,
                           writeback.waiters[oldest_waiter].request_id))
          {
            oldest_waiter = static_cast<int>(w);
          }
        }

        if (oldest_waiter < 0)
          continue;

        const WritebackAckWaiter &waiter = writeback.waiters[oldest_waiter];

        ResponseChoice candidate;
        candidate.owner = ResponseOwner::kWriteback;
        candidate.entry = i;
        candidate.waiter = static_cast<std::size_t>(oldest_waiter);
        candidate.arrival_order = waiter.arrival_order;

        candidate.response.id = waiter.request_id;
        candidate.response.origin_id = waiter.origin_id;
        candidate.response.sequence = waiter.sequence;
        candidate.response.generation = waiter.generation;
        candidate.response.address = waiter.address;
        candidate.response.size = waiter.size;
        candidate.response.type = ResponseType::kWritebackAck;
        candidate.response.data.clear();

        if (!have_best || ResponseLess(candidate, best))
        {
          best = candidate;
          have_best = true;
        }
      }

      if (!have_best)
      {
        return ResponseChoice();
      }
      return best;
      // end
    }

    // true iff request's range overlaps any
    // non-ACK_READY (i.e. UNSENT or IN_FLIGHT) writeback entry.
    bool ConflictsWithLowerIncompleteWriteback(const CacheRequest &request) const
    {
      (void)request;
      // start

      // if writeback conflicting with request is UNSENT or IN_FLIGHT, return true
      for (const WritebackEntry &writeback : current_writebacks_)
      {
        if (!writeback.valid)
        {
          continue;
        }
        if (writeback.state != OwnerState::kUnsent && writeback.state != OwnerState::kInFlight)
        {
          continue;
        }
        if (RangesOverlap(request.address, request.size, writeback.address, writeback.size))
        {
          return true;
        }
      }
      return false;

      // end
    }

    // select the oldest eligible UNSENT demand MSHR
    // (excluding any that overlap an incomplete writeback) and the oldest
    // eligible UNSENT writeback, sorted by (arrival_order, lower id). A
    // demand wins unless the writeback buffer has no free entry or the
    // oldest eligible writeback has already been bypassed by eight demand
    // grants, in which case that writeback must be selected instead. Must
    // not mutate state or allocate an id.
    // Existing helpers: ConflictsWithLowerIncompleteWriteback, FindFreeWriteback.
    RequestChoice SelectLowerRequest() const
    {
      // start

      RequestChoice result;
      bool have_demand = false;
      bool have_writeback = false;

      // oldest eligible UNSENT demand MSHR
      for (std::size_t i = 0; i < current_mshrs_.size(); ++i)
      {
        const MshrEntry &mshr = current_mshrs_[i];

        if (!mshr.valid || mshr.state != OwnerState::kUnsent)
        {
          continue;
        }

        // demand read incompatible with an incomplete writeback
        if (ConflictsWithLowerIncompleteWriteback(mshr.lower_request))
        {
          continue;
        }

        if (!have_demand ||
            std::tie(mshr.arrival_order, mshr.lower_request.id) <
                std::tie(result.arrival_order, result.request.id))
        {
          result.owner = RequestOwner::kMshr;
          result.entry = i;
          result.request = mshr.lower_request;
          result.arrival_order = mshr.arrival_order;

          have_demand = true;
        }
      }

      // oldest eligible UNSENT writeback
      std::size_t oldest_writeback = 0;

      for (std::size_t i = 0; i < current_writebacks_.size(); ++i)
      {
        const WritebackEntry &wb = current_writebacks_[i];

        if (!wb.valid || wb.state != OwnerState::kUnsent)
        {
          continue;
        }

        if (!have_writeback ||
            std::tie(wb.arrival_order, wb.lower_request.id) <
                std::tie(current_writebacks_[oldest_writeback].arrival_order,
                         current_writebacks_[oldest_writeback].lower_request.id))
        {
          oldest_writeback = i;
          have_writeback = true;
        }
      }

      // No lower request exists.
      if (!have_demand && !have_writeback)
      {
        return RequestChoice();
      }

      // If no demand, handle writeback.
      if (!have_demand)
      {
        const WritebackEntry &wb = current_writebacks_[oldest_writeback];

        result.owner = RequestOwner::kWriteback;
        result.entry = oldest_writeback;
        result.request = wb.lower_request;
        result.arrival_order = wb.arrival_order;
        result.oldest_eligible_writeback = oldest_writeback;

        return result;
      }

      // If no writeback, handle demand.
      if (!have_writeback)
      {
        result.demand_blocked_by_writeback = false;
        result.oldest_eligible_writeback = std::nullopt;
        return result;
      }

      const WritebackEntry &wb = current_writebacks_[oldest_writeback];

      result.oldest_eligible_writeback = oldest_writeback;

      /*Decide demand vs writeback*/
      // Demand normally wins.
      // Writeback is forced if:

      // 1) there is no free writeback entry
      const bool no_free_writeback =
          FindFreeWriteback() < 0;
      // OR
      // 2) the oldest eligible writeback has already accumulated
      //      eight accepted demand-grant bypasses
      const bool writeback_starved =
          wb.demand_bypasses >= 8;

      if (no_free_writeback || writeback_starved)
      {
        result.owner = RequestOwner::kWriteback;
        result.entry = oldest_writeback;
        result.request = wb.lower_request;
        result.arrival_order = wb.arrival_order;
        return result;
      }

      // Otherwise demand wins.
      result.demand_blocked_by_writeback = false;

      return result;

      // end
    }

    // available (already implemented, call from here):
    // SelectResponse() (above) does the candidate selection; this method
    // just extracts its response if any.
    std::optional<CacheResponse> ResponseIntent() const
    {
      // start

      const ResponseChoice choice = SelectResponse();
      if (choice.owner == ResponseOwner::kNone)
      {
        return std::nullopt;
      }
      return choice.response;

      // end
    }

    // available (already implemented, call from here):
    // SelectLowerRequest() (above) does the candidate selection; this
    // method just extracts its request if any.
    std::optional<CacheRequest> LowerRequestIntent() const
    {
      // start

      const RequestChoice choice = SelectLowerRequest();
      if (choice.owner == RequestOwner::kNone)
      {
        return std::nullopt;
      }
      return choice.request;

      // end
    }

    // mark the candidate ResponseIntent() offered as sent;
    // free an MSHR (unpinning its way) or writeback entry once every one of
    // its waiters has been sent.
    // Existing helpers: SelectResponse, ResponseIntent.
    void ApplyResponseGrant()
    {
      // start

      const ResponseChoice choice = SelectResponse();
      if (choice.owner == ResponseOwner::kNone)
      {
        throw std::runtime_error("ApplyResponseGrant called with no response candidate");
      }
      if (choice.owner == ResponseOwner::kOutput)
      {
        if (!next_output_.valid)
        {
          throw std::runtime_error("ApplyResponseGrant: OUTPUT is not valid");
        }
        next_output_ = OutputEntry();
        return;
      }

      if (choice.owner == ResponseOwner::kMshr)
      {
        if (choice.entry >= next_mshrs_.size())
        {
          throw std::runtime_error("ApplyResponseGrant: invalid MSHR index");
        }

        MshrEntry &mshr = next_mshrs_[choice.entry];

        if (!mshr.valid ||
            mshr.state != OwnerState::kResponding ||
            choice.waiter >= mshr.waiters.size())
        {
          throw std::runtime_error("ApplyResponseGrant: invalid MSHR response candidate");
        }

        MshrWaiter &waiter = mshr.waiters[choice.waiter];

        if (!waiter.response_ready || waiter.response_sent)
        {
          throw std::runtime_error("ApplyResponseGrant: MSHR waiter is not sendable");
        }

        waiter.response_sent = true;

        bool all_sent = true;
        for (const MshrWaiter &w : mshr.waiters)
        {
          if (!w.response_sent)
          {
            all_sent = false;
            break;
          }
        }

        if (all_sent)
        {
          const std::uint32_t set = mshr.set;
          const std::uint32_t way = mshr.reserved_way;

          if (set >= config_.sets || way >= config_.ways)
          {
            throw std::runtime_error("ApplyResponseGrant: invalid MSHR cache location");
          }

          // The MSHR no longer owns the line, so release its reservation.
          next_lines_[set][way].reserved = false;

          // Free the MSHR.
          mshr = MshrEntry();
        }

        return;
      }

      if (choice.owner == ResponseOwner::kWriteback)
      {
        if (choice.entry >= next_writebacks_.size())
        {
          throw std::runtime_error("ApplyResponseGrant: invalid writeback index");
        }

        WritebackEntry &writeback =
            next_writebacks_[choice.entry];

        if (!writeback.valid ||
            writeback.state != OwnerState::kAckReady ||
            choice.waiter >= writeback.waiters.size())
        {
          throw std::runtime_error("ApplyResponseGrant: invalid writeback response candidate");
        }

        WritebackAckWaiter &waiter = writeback.waiters[choice.waiter];

        if (waiter.response_sent)
        {
          throw std::runtime_error("ApplyResponseGrant: writeback waiter already sent");
        }

        waiter.response_sent = true;

        bool all_sent = true;
        for (const WritebackAckWaiter &w : writeback.waiters)
        {
          if (!w.response_sent)
          {
            all_sent = false;
            break;
          }
        }

        if (all_sent)
        {
          next_writebacks_[choice.entry] = WritebackEntry();
        }

        return;
      }

      throw std::runtime_error("ApplyResponseGrant: unknown response owner");

      // end
    }

    void ValidateEcho(const CacheResponse &response,
                      const CacheRequest &request,
                      ResponseType expected_type) const
    {
      if (response.id != request.id ||
          response.origin_id != request.origin_id ||
          response.sequence != request.sequence ||
          response.generation != request.generation ||
          response.address != request.address || response.size != request.size ||
          response.type != expected_type)
      {
        throw std::runtime_error("lower response metadata does not echo request");
      }
    }

    // for an owned fill -- install the complete line
    // into its already-reserved way, replay every waiter once in arrival
    // order at byte granularity (capturing each load's bytes at its own
    // replay point, applying each store's bytes), mark dirty if any waiter
    // wrote, call replacement_->OnInsert exactly once. For an owned
    // writeback ack -- transition ACK_READY (or free immediately if it had
    // no waiters).
    // Existing helpers: FindMshrByLowerId, FindWritebackByLowerId, ValidateEcho.
    void ApplyLowerResponse(const CacheResponse &response)
    {
      (void)response;
      // start

      // MSHR fill response
      const int mshr_index = FindMshrByLowerId(response.id);

      if (mshr_index >= 0)
      {
        MshrEntry &mshr = next_mshrs_[static_cast<std::size_t>(mshr_index)];
        if (mshr.state != OwnerState::kInFlight)
        {
          throw std::runtime_error("MSHR lower response received for non-IN_FLIGHT MSHR");
        }

        ValidateEcho(response, mshr.lower_request, ResponseType::kReadData);

        if (response.data.size() != config_.line_size)
        {
          throw std::runtime_error("MSHR fill response is not a complete cache line");
        }

        const std::uint32_t set = mshr.set;
        const std::uint32_t way = mshr.reserved_way;
        if (set >= config_.sets || way >= config_.ways)
        {
          throw std::runtime_error("MSHR reserved cache location is out of range");
        }

        CacheLine &line = next_lines_[set][way];
        if (!line.reserved)
        {
          throw std::runtime_error("MSHR fill target is not reserved");
        }

        // Install the complete line returned by the lower cache.
        line.valid = true;
        line.tag = mshr.tag;
        line.data = response.data;
        line.dirty = false;
        // Keep the filled way pinned until all MSHR waiter responses drain.
        line.reserved = true;

        // Replay waiters in arrival order.
        std::vector<std::size_t> order(mshr.waiters.size());
        for (std::size_t i = 0; i < order.size(); ++i)
        {
          order[i] = i;
        }

        std::sort(order.begin(), order.end(),
                  [&](std::size_t a, std::size_t b)
                  {
                    return std::tie(mshr.waiters[a].arrival_order,
                                    mshr.waiters[a].request_id) <
                           std::tie(mshr.waiters[b].arrival_order,
                                    mshr.waiters[b].request_id);
                  });

        for (const std::size_t waiter_index : order)
        {
          MshrWaiter &waiter = mshr.waiters[waiter_index];
          const std::uint32_t offset = waiter.address % config_.line_size;

          if (offset + waiter.size > config_.line_size)
          {
            throw std::runtime_error("MSHR waiter crosses local cache line");
          }

          if (waiter.type == RequestType::kRead)
          {
            waiter.response_data.assign(
                line.data.begin() + offset,
                line.data.begin() + offset + waiter.size);

            waiter.response_ready = true;
          }
          else if (waiter.type == RequestType::kWrite)
          {
            if (waiter.data.size() != waiter.size)
            {
              throw std::runtime_error("MSHR write waiter has invalid payload");
            }

            std::copy(
                waiter.data.begin(),
                waiter.data.end(),
                line.data.begin() + offset);

            line.dirty = true;
            waiter.response_ready = true;
          }
          else
          {
            throw std::runtime_error("MSHR contains illegal waiter type");
          }
        }

        // The line stays pinned until all responses are sent.
        mshr.state = OwnerState::kResponding;
        replacement_->OnInsert(set, way);

        return;
      }

      // writeback ack response
      const int writeback_index = FindWritebackByLowerId(response.id);

      if (writeback_index >= 0)
      {
        WritebackEntry &writeback = next_writebacks_[static_cast<std::size_t>(writeback_index)];

        if (writeback.state != OwnerState::kInFlight)
        {
          throw std::runtime_error("writeback response received for non-IN_FLIGHT writeback");
        }

        ValidateEcho(response,
                     writeback.lower_request,
                     ResponseType::kWritebackAck);

        if (writeback.waiters.empty())
        {
          // Nothing needs to be acknowledged upward.
          writeback = WritebackEntry();
        }
        else
        {
          // Lower level is done; wait for upper responses.
          writeback.state = OwnerState::kAckReady;
        }

        return;
      }

      throw std::runtime_error("lower response does not belong to an MSHR or writeback");

      // end
    }

    // insert a new writeback entry, or merge into the
    // transitive-overlap survivor chosen by a matching PlanWriteback() call
    // -- oldest entry/ID survives, bytes applied in entry-age order with the
    // newly-accepted bytes winning any overlap (this call's data is newest),
    // combined waiters bounded and sorted by (arrival_order, request id).
    void InsertWriteback(std::uint32_t address,
                         const std::vector<std::uint8_t> &data,
                         std::uint64_t arrival_order,
                         const std::optional<WritebackAckWaiter> &waiter,
                         int granted_writeback = -1,
                         std::optional<std::uint64_t> preallocated_id =
                             std::nullopt)
    {
      (void)address;
      (void)data;
      (void)arrival_order;
      (void)waiter;
      (void)granted_writeback;
      (void)preallocated_id;
      // start

      if (data.empty())
      {
        throw std::runtime_error("InsertWriteback: empty writeback data");
      }
      const std::uint32_t size = static_cast<std::uint32_t>(data.size());

      // determine new-vs-merge
      const WritebackPlan plan =
          PlanWriteback(address,
                        size,
                        waiter.has_value() ? 1 : 0,
                        granted_writeback);
      if (!plan.allowed)
      {
        throw std::runtime_error("InsertWriteback: writeback plan is not allowed");
      }

      // No overlap -> new writeback entry
      if (plan.merge_entries.empty())
      {
        const int free_index = FindFreeWriteback();
        if (free_index < 0)
        {
          throw std::runtime_error("InsertWriteback: no free writeback entry");
        }
        // A brand-new lower owner needs an ID.
        if (!preallocated_id.has_value())
        {
          const std::vector<std::uint64_t> ids = transaction_ids_->ReserveBatch(SourceForRole(role_), 1);
          preallocated_id = ids[0];
        }

        WritebackEntry &entry = next_writebacks_[static_cast<std::size_t>(free_index)];
        entry = WritebackEntry();
        entry.valid = true;
        entry.address = address;
        entry.size = size;
        entry.data = data;
        entry.arrival_order = arrival_order;
        entry.state = OwnerState::kUnsent;
        entry.demand_bypasses = 0;
        entry.lower_request = CacheRequest();
        entry.lower_request.id = *preallocated_id;
        entry.lower_request.origin_id = kNoOriginId;
        entry.lower_request.sequence = kNoSequence;
        entry.lower_request.generation = 0;
        entry.lower_request.address = address;
        entry.lower_request.size = size;
        entry.lower_request.type = RequestType::kWriteback;
        entry.lower_request.data = data;

        if (waiter.has_value())
        {
          entry.waiters.push_back(*waiter);
        }
        return;
      }

      // merge Existing UNSENT writebacks
      if (plan.survivor < 0 || static_cast<std::size_t>(plan.survivor) >= current_writebacks_.size())
      {
        throw std::runtime_error("InsertWriteback: invalid survivor");
      }

      // A preallocated ID is only used when creating a new owner.
      if (preallocated_id.has_value())
      {
        throw std::runtime_error("InsertWriteback: preallocated ID supplied for merge");
      }

      const std::size_t survivor_index = static_cast<std::size_t>(plan.survivor);

      // PlanWriteback() already gave us exactly the entries that
      // belong to the merge closure. We only sort them by age so
      // that older bytes are applied first.
      std::vector<std::size_t> ordered = plan.merge_entries;

      std::sort(ordered.begin(), ordered.end(),
                [&](std::size_t a, std::size_t b)
                {
                  const WritebackEntry &wa =
                      current_writebacks_[a];
                  const WritebackEntry &wb =
                      current_writebacks_[b];

                  return std::tie(wa.arrival_order,
                                  wa.lower_request.id) <
                         std::tie(wb.arrival_order,
                                  wb.lower_request.id);
                });

      // finding ovelapping range
      std::uint64_t merged_begin = address;
      std::uint64_t merged_end = static_cast<std::uint64_t>(address) + size;

      for (std::size_t index : ordered)
      {
        const WritebackEntry &wb = current_writebacks_[index];
        merged_begin = std::min<std::uint64_t>(merged_begin, wb.address);
        merged_end = std::max<std::uint64_t>(merged_end, static_cast<std::uint64_t>(wb.address) + wb.size);
      }

      const std::uint64_t merged_size = merged_end - merged_begin;
      if (merged_size > std::numeric_limits<std::uint32_t>::max())
      {
        throw std::runtime_error("InsertWriteback: merged range too large");
      }

      const std::uint32_t merged_address = static_cast<std::uint32_t>(merged_begin);
      const std::uint32_t merged_size_u32 = static_cast<std::uint32_t>(merged_size);
      std::vector<std::uint8_t> merged_data(merged_size_u32, 0);

      // Apply existing writebacks oldest -> newest.
      for (std::size_t index : ordered)
      {
        const WritebackEntry &wb = current_writebacks_[index];
        const std::size_t destination =
            static_cast<std::size_t>(static_cast<std::uint64_t>(wb.address) - merged_begin);

        std::copy(
            wb.data.begin(),
            wb.data.end(),
            merged_data.begin() + destination);
      }

      const std::size_t incoming_destination =
          static_cast<std::size_t>(static_cast<std::uint64_t>(address) - merged_begin);

      std::copy(
          data.begin(),
          data.end(),
          merged_data.begin() + incoming_destination);

      // Start from the oldest survivor's entry so that its ID,
      // arrival order, and owner identity survive the merge.
      WritebackEntry survivor = current_writebacks_[survivor_index];

      survivor.address = merged_address;
      survivor.size = merged_size_u32;
      survivor.data = std::move(merged_data);
      survivor.state = OwnerState::kUnsent;
      survivor.demand_bypasses = 0;

      // The lower request keeps the survivor's original ID.
      survivor.lower_request.address = merged_address;
      survivor.lower_request.size = merged_size_u32;
      survivor.lower_request.type = RequestType::kWriteback;
      survivor.lower_request.data = survivor.data;

      // Collect waiters from every writeback being merged.
      survivor.waiters.clear();

      for (std::size_t index : ordered)
      {
        const WritebackEntry &wb =
            current_writebacks_[index];

        survivor.waiters.insert(
            survivor.waiters.end(),
            wb.waiters.begin(),
            wb.waiters.end());
      }

      if (waiter.has_value())
      {
        survivor.waiters.push_back(*waiter);
      }

      // The combined waiter list must stay deterministic.
      std::sort(
          survivor.waiters.begin(),
          survivor.waiters.end(),
          [](const WritebackAckWaiter &a,
             const WritebackAckWaiter &b)
          {
            return std::tie(a.arrival_order, a.request_id) <
                   std::tie(b.arrival_order, b.request_id);
          });

      if (survivor.waiters.size() >
          static_cast<std::size_t>(config_.writeback_waiters_per_entry))
      {
        throw std::runtime_error("InsertWriteback: waiter capacity exceeded");
      }

      // Write the merged survivor into PLANNED state.
      next_writebacks_[survivor_index] = std::move(survivor);

      // All other entries in the merge group are absorbed.
      for (std::size_t index : ordered)
      {
        if (index == survivor_index)
        {
          continue;
        }

        next_writebacks_[index] = WritebackEntry();
      }

      ++next_stats_.writeback_merges;

      // end
    }

    // apply a committed hit -- write store/writeback bytes
    // at the request's offset, mark dirty, build the response (read the
    // pre-write line for a load, the post-write line otherwise), call
    // replacement_->OnHit, and update hits/writeback_hits exactly once.
    // Existing helpers: ResponseForHit.
    void ApplyHit(const LookupDecision &decision)
    {
      (void)decision;
      // start
      if (!current_lookup_.valid)
      {
        throw std::runtime_error("ApplyHit called with empty LOOKUP");
      }

      if (decision.hit_way < 0 ||
          static_cast<std::uint32_t>(decision.hit_way) >= config_.ways)
      {
        throw std::runtime_error("ApplyHit called with invalid hit way");
      }

      const PipelineEntry &pipeline = current_lookup_;
      const std::uint32_t set = pipeline.address.set;
      const std::uint32_t way =
          static_cast<std::uint32_t>(decision.hit_way);

      // Work on planned state, not current state.
      CacheLine &line = next_lines_[set][way];

      if (!line.valid || line.tag != pipeline.address.tag)
      {
        throw std::runtime_error("ApplyHit target is not a valid cache hit");
      }

      CacheResponse response;
      // read
      if (pipeline.request.type == RequestType::kRead)
      {
        response = ResponseForHit(pipeline, line);
      }
      // write/writeback -> mark dirty, modify bytes, send ack
      else if (pipeline.request.type == RequestType::kWrite ||
               pipeline.request.type == RequestType::kWriteback)
      {
        std::copy(
            pipeline.request.data.begin(),
            pipeline.request.data.end(),
            line.data.begin() + pipeline.address.offset);
        line.dirty = true;
        response = ResponseForHit(pipeline, line);
      }
      else
      {
        throw std::runtime_error("illegal request type in ApplyHit");
      }

      if (next_output_.valid)
      {
        throw std::runtime_error("OUTPUT occupied when committing a hit");
      }

      next_output_.valid = true;
      next_output_.response = std::move(response);
      next_output_.arrival_order = AllocateArrival();

      // Replacement state changes are planned for this cycle.
      replacement_->OnHit(set, way);

      // Count this hit exactly once.
      ++next_stats_.hits;

      if (pipeline.request.type == RequestType::kWriteback)
      {
        ++next_stats_.writeback_hits;
      }
      next_lookup_.valid = false;

      // end
    }

    // append a waiter to the existing MSHR; update
    // mshr_merges exactly once; clear LOOKUP.
    void ApplyMshrMerge(int mshr_index)
    {
      (void)mshr_index;
      // start

      if (!current_lookup_.valid)
      {
        throw std::runtime_error("ApplyMshrMerge called with empty LOOKUP");
      }
      if (mshr_index < 0 || static_cast<std::size_t>(mshr_index) >= next_mshrs_.size())
      {
        throw std::runtime_error("ApplyMshrMerge called with invalid MSHR index");
      }
      MshrEntry &mshr = next_mshrs_[static_cast<std::size_t>(mshr_index)];
      if (!mshr.valid)
      {
        throw std::runtime_error("ApplyMshrMerge target MSHR is not valid");
      }
      if (mshr.waiters.size() >= static_cast<std::size_t>(config_.mshr_waiters_per_entry))
      {
        throw std::runtime_error("ApplyMshrMerge called with full waiter list");
      }

      const MshrWaiter waiter = MakeMshrWaiter(current_lookup_);
      mshr.waiters.push_back(waiter);
      ++next_stats_.mshr_merges;
      next_lookup_.valid = false;

      // end
    }

    // allocate the new primary MSHR at the planned victim
    // way, reserving it; if the victim line is valid+dirty, insert its
    // writeback (allocating a second id in the same batch when a new
    // writeback owner is created, via transaction_ids_->ReserveBatch);
    // create one full-local-line lower kRead with one retained id; update
    // evictions/dirty_evictions/primary_misses exactly once each as
    // applicable; clear LOOKUP.
    // Existing helpers: SelectVictim, PeekVictim, MakeMshrWaiter, InsertWriteback.
    void ApplyMshrAllocation(const LookupDecision &decision,
                             int granted_writeback)
    {
      (void)decision;
      (void)granted_writeback;
      // start

      if (!current_lookup_.valid)
      {
        throw std::runtime_error("ApplyMshrAllocation called with empty LOOKUP");
      }
      if (decision.kind != LookupKind::kMshrAllocate)
      {
        throw std::runtime_error("ApplyMshrAllocation called with wrong lookup decision");
      }
      if (decision.mshr < 0 ||
          static_cast<std::size_t>(decision.mshr) >= next_mshrs_.size())
      {
        throw std::runtime_error("ApplyMshrAllocation: invalid MSHR index");
      }
      if (decision.victim_way < 0 ||
          static_cast<std::uint32_t>(decision.victim_way) >= config_.ways)
      {
        throw std::runtime_error("ApplyMshrAllocation: invalid victim way");
      }

      const PipelineEntry &pipeline = current_lookup_;
      const CacheRequest &request = pipeline.request;

      const std::uint32_t set = pipeline.address.set;
      const std::uint32_t way = static_cast<std::uint32_t>(decision.victim_way);

      const CacheLine &victim = current_lines_[set][way];
      if (victim.reserved)
      {
        throw std::runtime_error("ApplyMshrAllocation: victim way is already reserved");
      }

      MshrEntry &mshr = next_mshrs_[static_cast<std::size_t>(decision.mshr)];
      if (mshr.valid)
      {
        throw std::runtime_error("ApplyMshrAllocation: selected MSHR is not free");
      }

      const bool dirty_victim = victim.valid && victim.dirty;
      std::uint32_t ids_needed = 1;
      std::uint64_t writeback_id = 0;
      std::uint64_t mshr_id = 0;
      int selected_writeback = -1;

      if (dirty_victim)
      {
        const std::uint32_t victim_block = victim.tag * config_.sets + set;
        const std::uint32_t victim_address = victim_block * config_.line_size;
        const WritebackPlan plan =
            PlanWriteback(victim_address,
                          config_.line_size,
                          /*new_waiters=*/0,
                          granted_writeback);
        if (!plan.allowed)
        {
          throw std::runtime_error("ApplyMshrAllocation: dirty victim writeback is not allowed");
        }
        // If there is no existing survivor, a new WB owner is needed.
        if (plan.survivor < 0)
        {
          ids_needed = 2;
        }
        else
        {
          selected_writeback = plan.survivor;
        }
      }

      const std::vector<std::uint64_t> ids = transaction_ids_->ReserveBatch(SourceForRole(role_), ids_needed);
      if (dirty_victim)
      {
        if (ids_needed == 2)
        {
          writeback_id = ids[0];
          mshr_id = ids[1];
        }
        else
        {
          mshr_id = ids[0];
        }
      }
      else
      {
        mshr_id = ids[0];
      }

      // Actually select and reserve the victim in planned state.
      const int actual_victim = SelectVictim(set);
      if (actual_victim != decision.victim_way)
      {
        throw std::runtime_error("ApplyMshrAllocation: victim changed between plan and apply");
      }

      // Create the dirty-victim writeback, if necessary.
      if (dirty_victim)
      {
        const std::uint32_t victim_block =
            victim.tag * config_.sets + set;

        const std::uint32_t victim_address =
            victim_block * config_.line_size;

        std::optional<std::uint64_t> preallocated_id;

        if (selected_writeback < 0)
        {
          preallocated_id = writeback_id;
        }

        InsertWriteback(
            victim_address,
            victim.data,
            AllocateArrival(),
            std::nullopt,
            granted_writeback,
            preallocated_id);

        ++next_stats_.dirty_evictions;
        ++next_stats_.writebacks_generated;
      }

      if (victim.valid)
      {
        ++next_stats_.evictions;
      }

      // Once the old victim bytes (if dirty) are safely captured in the
      // writeback structure, the reserved way belongs to this new MSHR.
      // Invalidate the old tag immediately so an incoming writeback cannot
      // falsely hit stale victim data while the replacement fill is pending.
      next_lines_[set][way].valid = false;
      next_lines_[set][way].dirty = false;
      next_lines_[set][way].reserved = true;

      // Allocate the MSHR.
      mshr.valid = true;
      mshr.block_address = pipeline.address.block;
      mshr.set = set;
      mshr.tag = pipeline.address.tag;
      mshr.reserved_way = way;
      mshr.arrival_order = pipeline.arrival_order;
      mshr.state = OwnerState::kUnsent;

      // The original request becomes the first waiter.
      mshr.waiters.clear();
      mshr.waiters.push_back(MakeMshrWaiter(pipeline));

      // Create ONE full-line lower read.
      mshr.lower_request = CacheRequest();
      mshr.lower_request.id = mshr_id;
      mshr.lower_request.origin_id = request.origin_id;
      mshr.lower_request.sequence = request.sequence; 
      mshr.lower_request.generation = 0;
      mshr.lower_request.address = pipeline.address.block * config_.line_size;
      mshr.lower_request.size = config_.line_size;
      mshr.lower_request.type = RequestType::kRead;
      mshr.lower_request.data.clear();

      ++next_stats_.primary_misses;
      next_lookup_.valid = false;

      // end
    }

    // forward an upper writeback into InsertWriteback with
    // its own ack waiter; clear LOOKUP.
    // Existing helpers: MakeWritebackWaiter.
    void ApplyWritebackForward(int granted_writeback)
    {
      (void)granted_writeback;
      // start

      if (!current_lookup_.valid)
      {
        throw std::runtime_error("CacheController::ApplyWritebackForward: no LOOKUP entry");
      }
      const PipelineEntry &pipeline = current_lookup_;
      if (pipeline.request.type != RequestType::kWriteback)
      {
        throw std::runtime_error("CacheController::ApplyWritebackForward: request is not a writeback");
      }
      if (pipeline.request.size == 0 || pipeline.request.data.size() != pipeline.request.size)
      {
        throw std::runtime_error("CacheController::ApplyWritebackForward: invalid writeback payload");
      }

      const WritebackAckWaiter waiter = MakeWritebackWaiter(pipeline);

      InsertWriteback(
          pipeline.request.address,
          pipeline.request.data,
          pipeline.arrival_order,
          waiter,
          granted_writeback);

      next_lookup_.valid = false;

      // end
    }

    // increment exactly the one stats counter
    // corresponding to `stall` (kNone/kOutput count nothing). See SPEC.md's
    // stall-precedence chain for which counter fires when several reasons
    // could apply.
    void CountLookupStall(LookupStall stall)
    {
      (void)stall;
      // start

      switch (stall)
      {
      case LookupStall::kNone:
      case LookupStall::kOutput:
        return;

      case LookupStall::kMshrWaiterFull:
        ++next_stats_.mshr_waiter_full_stalls;
        return;

      case LookupStall::kMshrFull:
        ++next_stats_.mshr_full_stalls;
        return;

      case LookupStall::kReservedWay:
        ++next_stats_.reserved_way_stalls;
        return;

      case LookupStall::kWritebackOrder:
        ++next_stats_.writeback_order_stalls;
        return;

      case LookupStall::kWritebackWaiterFull:
        ++next_stats_.writeback_waiter_full_stalls;
        return;

      case LookupStall::kWritebackFull:
        ++next_stats_.writeback_full_stalls;
        return;
      }

      throw std::runtime_error("CountLookupStall: unknown stall reason");

      // end
    }

    // transition the granted MSHR/writeback UNSENT ->
    // IN_FLIGHT. Granting a demand bumps the oldest eligible writeback's
    // demand_bypasses (capped at 8); granting a writeback resets
    // demand_bypasses to 0 and counts writebacks_sent.
    void ApplyLowerRequestGrant()
    {
      // start

      const RequestChoice choice = SelectLowerRequest();

      if (choice.owner == RequestOwner::kNone)
      {
        throw std::runtime_error("ApplyLowerRequestGrant called with no lower request");
      }

      if (choice.owner == RequestOwner::kMshr)
      {
        if (choice.entry >= next_mshrs_.size())
        {
          throw std::runtime_error("ApplyLowerRequestGrant: invalid MSHR index");
        }

        MshrEntry &mshr = next_mshrs_[choice.entry];

        if (!mshr.valid || mshr.state != OwnerState::kUnsent)
        {
          throw std::runtime_error("ApplyLowerRequestGrant: MSHR is not UNSENT");
        }

        if (mshr.lower_request.id != choice.request.id)
        {
          throw std::runtime_error("ApplyLowerRequestGrant: MSHR request mismatch");
        }

        mshr.state = OwnerState::kInFlight;

        // This demand has bypassed the oldest eligible writeback once.
        if (choice.oldest_eligible_writeback.has_value())
        {
          const std::size_t wb_index =
              *choice.oldest_eligible_writeback;

          if (wb_index >= next_writebacks_.size())
          {
            throw std::runtime_error("ApplyLowerRequestGrant: invalid bypass writeback index");
          }

          WritebackEntry &wb = next_writebacks_[wb_index];

          if (!wb.valid || wb.state != OwnerState::kUnsent)
          {
            throw std::runtime_error("ApplyLowerRequestGrant: bypass writeback is not UNSENT");
          }

          if (wb.demand_bypasses < 8)
          {
            ++wb.demand_bypasses;
          }
        }

        return;
      }

      if (choice.owner == RequestOwner::kWriteback)
      {
        if (choice.entry >= next_writebacks_.size())
        {
          throw std::runtime_error("ApplyLowerRequestGrant: invalid writeback index");
        }

        WritebackEntry &wb =
            next_writebacks_[choice.entry];

        if (!wb.valid || wb.state != OwnerState::kUnsent)
        {
          throw std::runtime_error("ApplyLowerRequestGrant: writeback is not UNSENT");
        }

        if (wb.lower_request.id != choice.request.id)
        {
          throw std::runtime_error("ApplyLowerRequestGrant: writeback request mismatch");
        }

        wb.state = OwnerState::kInFlight;

        // Successful WB service resets starvation tracking.
        wb.demand_bypasses = 0;

        ++next_stats_.writebacks_sent;

        return;
      }

      throw std::runtime_error("ApplyLowerRequestGrant: unknown request owner");

      // end
    }

    // the single mutating call per cycle. Apply only the
    // transfers `grants` authorizes, computed from ONE consistent snapshot of
    // current_* state: upper-response removal (ApplyResponseGrant), owned
    // fill/writeback-ack installation (ApplyLowerResponse), at most one
    // parallel LOOKUP decision or its exact stall (DecideLookup +
    // ApplyHit/ApplyMshrMerge/ApplyMshrAllocation/ApplyWritebackForward/
    // CountLookupStall -- note a fill takes the LOOKUP port, so it must not
    // additionally run), back-to-front INDEX->LOOKUP movement plus admission
    // of upper_request into INDEX, and the granted lower-request owner
    // transition (ApplyLowerRequestGrant). Update every counter exactly
    // once, at its defined event point. Throw for any grant referring to a
    // nonexistent candidate, or any owner/echo mismatch -- see ABI.md's
    // error taxonomy.
    // Existing helpers: ResponseIntent, LowerRequestIntent, StageIntent, ValidateInbound, AllocateArrival, MapAddress.
    void Operate(std::uint64_t current_cycle,
                 const CacheRequest *upper_request,
                 const CacheResponse *lower_response,
                 const CacheOperateGrants &grants)
    {
      (void)current_cycle;
      (void)upper_request;
      (void)lower_response;
      (void)grants;
      if (!begun_ || operated_)
      {
        throw std::runtime_error("cache Operate lifecycle violation");
      }
      // start

      const ResponseChoice response_choice = SelectResponse();
      const RequestChoice request_choice = SelectLowerRequest();
      const CacheStageIntent stage =
          StageIntent(
              upper_request,
              lower_response,
              grants.upper_response,
              grants.lower_request);

      if (grants.upper_request)
      {
        if (upper_request == nullptr ||
            !stage.accept_upper_request)
        {
          throw std::runtime_error("CacheController::Operate: invalid upper-request grant");
        }
      }

      if (grants.lower_response)
      {
        if (lower_response == nullptr ||
            !stage.accept_lower_response)
        {
          throw std::runtime_error("CacheController::Operate: invalid lower-response grant");
        }
      }

      if (grants.upper_response &&
          response_choice.owner == ResponseOwner::kNone)
      {
        throw std::runtime_error("CacheController::Operate: upper-response grant without candidate");
      }

      if (grants.lower_request)
      {
        if (request_choice.owner == RequestOwner::kNone ||
            !grants.lower_request_queue_available)
        {
          throw std::runtime_error("CacheController::Operate: lower-request grant without candidate/capacity");
        }
      }

      // Upper response transfer.
      if (grants.upper_response)
      {
        ApplyResponseGrant();
      }

      // Lower response transfer.
      bool lower_fill_accepted = false;
      if (grants.lower_response)
      {
        const int mshr = FindMshrByLowerId(lower_response->id);
        lower_fill_accepted = (mshr >= 0);
        ApplyLowerResponse(*lower_response);
        if (lower_fill_accepted)
        {
          ++next_stats_.fills;
        }
      }

      // Process the normal LOOKUP port.
      if (!lower_fill_accepted & current_lookup_.valid)
      {
        const bool output_space = !current_output_.valid || OutputWillDrain(grants.upper_response);

        int granted_writeback = -1;
        if (grants.lower_request && request_choice.owner == RequestOwner::kWriteback)
        {
          granted_writeback = static_cast<int>(request_choice.entry);
        }

        const LookupDecision decision = DecideLookup(output_space, granted_writeback);

        switch (decision.kind)
        {

        case LookupKind::kEmpty:
          break;

        case LookupKind::kHit:
          ApplyHit(decision);
          break;

        case LookupKind::kMshrMerge:
          ApplyMshrMerge(decision.mshr);
          break;

        case LookupKind::kMshrAllocate:
          ApplyMshrAllocation(
              decision,
              granted_writeback);
          break;

        case LookupKind::kWritebackForward:
          ApplyWritebackForward(
              granted_writeback);
          break;

        case LookupKind::kHold:
          CountLookupStall(decision.stall);
          break;
        }
      }

      // Move INDEX -> LOOKUP.
      if (current_index_.valid && !next_lookup_.valid)
      {
        next_lookup_ = current_index_;
        next_index_.valid = false;
      }

      // new upper request into INDEX.
      if (grants.upper_request)
      {
        ValidateInbound(*upper_request);
        if (next_index_.valid)
        {
          throw std::runtime_error("CacheController::Operate: INDEX occupied on admission");
        }

        PipelineEntry entry;

        entry.valid = true;
        entry.request = *upper_request;
        entry.arrival_order = AllocateArrival();
        entry.address = MapAddress(upper_request->address);

        next_index_ = std::move(entry);

        if (upper_request->type == RequestType::kWriteback)
        {
          ++next_stats_.writeback_requests_accepted;
        }
        else
        {
          ++next_stats_.demand_accesses_accepted;
        }
      }

      if (grants.lower_request)
      {
        ApplyLowerRequestGrant();
      }

      operated_ = true;

      // end
    }

    void Commit()
    {
      if (!begun_ || !operated_)
      {
        throw std::runtime_error("cache Commit lifecycle violation");
      }
      replacement_->Commit();
      current_lines_ = std::move(next_lines_);
      current_index_ = std::move(next_index_);
      current_lookup_ = std::move(next_lookup_);
      current_output_ = std::move(next_output_);
      current_mshrs_ = std::move(next_mshrs_);
      current_writebacks_ = std::move(next_writebacks_);
      current_arrival_ = next_arrival_;
      stats_ = next_stats_;
      begun_ = false;
      operated_ = false;
    }

    CacheSnapshot Snapshot() const
    {
      CacheSnapshot snapshot;
      const PipelineEntry entries[2] = {current_index_, current_lookup_};
      for (int i = 0; i < 2; ++i)
      {
        snapshot.pipeline[i].valid = entries[i].valid;
        if (entries[i].valid)
        {
          snapshot.pipeline[i].request_id = entries[i].request.id;
          snapshot.pipeline[i].address = entries[i].request.address;
          snapshot.pipeline[i].state = i == 0 ? "INDEX" : "LOOKUP";
        }
      }
      snapshot.pipeline[2].valid = current_output_.valid;
      if (current_output_.valid)
      {
        snapshot.pipeline[2].request_id = current_output_.response.id;
        snapshot.pipeline[2].address = current_output_.response.address;
        snapshot.pipeline[2].state = "OUTPUT";
      }
      for (const MshrEntry &entry : current_mshrs_)
      {
        MshrSnapshot row;
        row.waiter_capacity = config_.mshr_waiters_per_entry;
        if (entry.valid)
        {
          row.valid = true;
          row.block_address = entry.block_address;
          row.set = entry.set;
          row.tag = entry.tag;
          row.reserved_way = static_cast<int>(entry.reserved_way);
          row.lower_id = entry.lower_request.id;
          row.arrival_order = entry.arrival_order;
          row.state = OwnerStateName(entry.state);
          row.waiters = entry.waiters;
        }
        snapshot.mshrs.push_back(std::move(row));
      }
      for (const WritebackEntry &entry : current_writebacks_)
      {
        WritebackSnapshot row;
        row.waiter_capacity = config_.writeback_waiters_per_entry;
        if (entry.valid)
        {
          row.valid = true;
          row.address = entry.address;
          row.size = entry.size;
          row.lower_id = entry.lower_request.id;
          row.arrival_order = entry.arrival_order;
          row.demand_bypasses = entry.demand_bypasses;
          row.state = OwnerStateName(entry.state);
          row.data = entry.data;
          row.waiters = entry.waiters;
        }
        snapshot.writebacks.push_back(std::move(row));
      }
      for (std::uint32_t set = 0; set < config_.sets; ++set)
      {
        const std::vector<std::uint32_t> replacement =
            replacement_->DebugState(set);
        snapshot.replacement.push_back({set, replacement});
        for (std::uint32_t way = 0; way < config_.ways; ++way)
        {
          std::uint64_t owner = kNoOriginId;
          for (const MshrEntry &mshr : current_mshrs_)
          {
            if (mshr.valid && mshr.set == set && mshr.reserved_way == way)
            {
              owner = mshr.lower_request.id;
            }
          }
          snapshot.lines.push_back(
              {set, way, current_lines_[set][way], owner});
        }
      }
      return snapshot;
    }

    bool HasWork() const
    {
      if (current_index_.valid || current_lookup_.valid || current_output_.valid)
      {
        return true;
      }
      return std::any_of(current_mshrs_.begin(), current_mshrs_.end(),
                         [](const MshrEntry &entry)
                         { return entry.valid; }) ||
             std::any_of(current_writebacks_.begin(), current_writebacks_.end(),
                         [](const WritebackEntry &entry)
                         { return entry.valid; });
    }

    CacheRole role_;
    CacheConfig config_;
    TransactionIdService *transaction_ids_;
    std::vector<std::vector<CacheLine>> current_lines_;
    std::vector<std::vector<CacheLine>> next_lines_;
    PipelineEntry current_index_;
    PipelineEntry current_lookup_;
    OutputEntry current_output_;
    PipelineEntry next_index_;
    PipelineEntry next_lookup_;
    OutputEntry next_output_;
    std::vector<MshrEntry> current_mshrs_;
    std::vector<MshrEntry> next_mshrs_;
    std::vector<WritebackEntry> current_writebacks_;
    std::vector<WritebackEntry> next_writebacks_;
    std::uint64_t current_arrival_ = 0;
    std::uint64_t next_arrival_ = 0;
    CacheStats stats_;
    CacheStats next_stats_;
    std::unique_ptr<ReplacementPolicy> replacement_;
    bool begun_ = false;
    bool operated_ = false;
  };
} // namespace pipesim::cache
