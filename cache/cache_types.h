#ifndef PIPESIM_CACHE_CACHE_TYPES_H_
#define PIPESIM_CACHE_CACHE_TYPES_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pipesim {

    namespace cache {

        enum class RequestType {

            kRead,   kWrite,   kWriteback,
        };

        enum class ResponseType {

            kReadData,   kWriteAck,   kWritebackAck,
        };

        enum class CacheRole {

            kL1I,   kL2I,   kL1D,   kL2D,
        };

        enum class TransactionSource : std::uint8_t {

            kCpuInstruction = 0,   kCpuData = 1,   kL1I = 2,   kL1D = 3,   kL2I = 4,   kL2D = 5,
        };

        inline constexpr std::uint64_t kNoOriginId =     std::numeric_limits<std::uint64_t>::max();

        inline constexpr std::uint64_t kNoSequence =     std::numeric_limits<std::uint64_t>::max();

        struct CacheRequest {

            std::uint64_t id = 0;

            std::uint64_t origin_id = 0;

            std::uint64_t sequence = 0;

            std::uint32_t generation = 0;

            std::uint32_t address = 0;

            std::uint32_t size = 0;

            RequestType type = RequestType::kRead;

            std::vector<std::uint8_t> data;

        };

        struct CacheResponse {

            std::uint64_t id = 0;

            std::uint64_t origin_id = 0;

            std::uint64_t sequence = 0;

            std::uint32_t generation = 0;

            std::uint32_t address = 0;

            std::uint32_t size = 0;

            ResponseType type = ResponseType::kReadData;

            std::vector<std::uint8_t> data;

        };

        struct CacheLine {

            bool valid = false;

            bool dirty = false;

            bool reserved = false;

            std::uint32_t tag = 0;

            std::vector<std::uint8_t> data;

        };

        struct MshrWaiter {

            std::uint64_t request_id = 0;

            std::uint64_t origin_id = 0;

            std::uint64_t sequence = 0;

            std::uint64_t arrival_order = 0;

            std::uint32_t generation = 0;

            RequestType type = RequestType::kRead;

            std::uint32_t address = 0;

            std::uint32_t size = 0;

            std::vector<std::uint8_t> data;

            bool response_ready = false;

            bool response_sent = false;

            std::vector<std::uint8_t> response_data;

        };

        struct WritebackAckWaiter {

            std::uint64_t request_id = 0;

            std::uint64_t origin_id = 0;

            std::uint64_t sequence = 0;

            std::uint64_t arrival_order = 0;

            std::uint32_t generation = 0;

            std::uint32_t address = 0;

            std::uint32_t size = 0;

            bool response_sent = false;

        };

        struct CacheStats {

            std::uint64_t demand_accesses_accepted = 0;

            std::uint64_t writeback_requests_accepted = 0;

            std::uint64_t hits = 0;

            std::uint64_t writeback_hits = 0;

            std::uint64_t primary_misses = 0;

            std::uint64_t mshr_merges = 0;

            std::uint64_t mshr_full_stalls = 0;

            std::uint64_t mshr_waiter_full_stalls = 0;

            std::uint64_t reserved_way_stalls = 0;

            std::uint64_t writeback_full_stalls = 0;

            std::uint64_t writeback_waiter_full_stalls = 0;

            std::uint64_t fills = 0;

            std::uint64_t evictions = 0;

            std::uint64_t dirty_evictions = 0;

            std::uint64_t writebacks_generated = 0;

            std::uint64_t writebacks_sent = 0;

            std::uint64_t writeback_merges = 0;

            std::uint64_t writeback_conflict_stalls = 0;

            std::uint64_t writeback_order_stalls = 0;

            std::uint64_t input_queue_stalls = 0;

            std::uint64_t output_queue_stalls = 0;

        };

        struct PipelineSlotSnapshot {

            bool valid = false;

            std::uint64_t request_id = 0;

            std::uint32_t address = 0;

            std::string state;

        };

        struct MshrSnapshot {

            bool valid = false;

            std::uint32_t block_address = 0;

            std::uint32_t set = 0;

            std::uint32_t tag = 0;

            int reserved_way = -1;

            std::uint64_t lower_id = 0;

            std::uint64_t arrival_order = 0;

            std::string state;

            std::size_t waiter_capacity = 0;

            std::vector<MshrWaiter> waiters;

        };

        struct WritebackSnapshot {

            bool valid = false;

            std::uint32_t address = 0;

            std::uint32_t size = 0;

            std::uint64_t lower_id = 0;

            std::uint64_t arrival_order = 0;

            std::uint32_t demand_bypasses = 0;

            std::string state;

            std::size_t waiter_capacity = 0;

            std::vector<std::uint8_t> data;

            std::vector<WritebackAckWaiter> waiters;

        };

        struct LineSnapshot {

            std::uint32_t set = 0;

            std::uint32_t way = 0;

            CacheLine line;

            std::uint64_t reservation_owner = kNoOriginId;

        };

        struct ReplacementSnapshot {

            std::uint32_t set = 0;

            std::vector<std::uint32_t> state;

        };

        struct CacheSnapshot {

            std::array<PipelineSlotSnapshot, 3> pipeline;

            std::vector<MshrSnapshot> mshrs;

            std::vector<WritebackSnapshot> writebacks;

            std::vector<LineSnapshot> lines;

            std::vector<ReplacementSnapshot> replacement;

        };

        inline bool IsPowerOfTwo(std::uint64_t value) {

            return value != 0 && (value & (value - 1)) == 0;

        }

        inline std::uint32_t CheckedRangeEnd(std::uint32_t address,                                      std::uint32_t size) {

            if (size == 0) {

                throw std::runtime_error("zero-size cache transaction");

            }

            const std::uint64_t end = static_cast<std::uint64_t>(address) + size;

            if (end > (std::uint64_t {

                1
            }

            << 32)) {

                throw std::runtime_error("cache transaction address range wraps");

            }

            return static_cast<std::uint32_t>(end & 0xffffffffULL);

        }

        inline bool RangeFitsLine(std::uint32_t address, std::uint32_t size,                           std::uint32_t line_size) {

            if (size == 0 || line_size == 0) return false;

            const std::uint64_t end = static_cast<std::uint64_t>(address) + size;

            if (end > (std::uint64_t {

                1
            }

            << 32)) return false;

            const std::uint32_t block = address & ~(line_size - 1U);

            return end <= static_cast<std::uint64_t>(block) + line_size;

        }

        inline void ValidateRequestPayload(const CacheRequest& request) {

            CheckedRangeEnd(request.address, request.size);

            if (request.id == kNoOriginId) {

                throw std::runtime_error("cache request uses reserved transaction id");

            }

            if (request.type == RequestType::kRead) {

                if (!request.data.empty()) {

                    throw std::runtime_error("read request carries a payload");

                }

            }

            else if (request.data.size() != request.size) {

                throw std::runtime_error("write request payload size mismatch");

            }

            if (request.type == RequestType::kWriteback) {

                if (request.origin_id != kNoOriginId ||         request.sequence != kNoSequence || request.generation != 0) {

                    throw std::runtime_error("writeback carries demand metadata");

                }

            }

            else if (request.origin_id == kNoOriginId ||              request.sequence == kNoSequence) {

                throw std::runtime_error("demand request lacks origin or sequence");

            }

        }

        inline void ValidateResponsePayload(const CacheResponse& response) {

            CheckedRangeEnd(response.address, response.size);

            if (response.id == kNoOriginId) {

                throw std::runtime_error("cache response uses reserved transaction id");

            }

            if (response.type == ResponseType::kReadData) {

                if (response.data.size() != response.size) {

                    throw std::runtime_error("read response payload size mismatch");

                }

            }

            else if (!response.data.empty()) {

                throw std::runtime_error("acknowledgement carries a payload");

            }

        }

        inline std::vector<std::uint8_t> WordToBytes(std::uint32_t value) {

            return {

                static_cast<std::uint8_t>(value & 0xffU),       static_cast<std::uint8_t>((value >> 8) & 0xffU),       static_cast<std::uint8_t>((value >> 16) & 0xffU),       static_cast<std::uint8_t>((value >> 24) & 0xffU),
            };

        }

        inline std::uint32_t BytesToWord(const std::vector<std::uint8_t>& data) {

            if (data.size() != 4) {

                throw std::runtime_error("word payload must contain exactly four bytes");

            }

            return static_cast<std::uint32_t>(data[0]) |          (static_cast<std::uint32_t>(data[1]) << 8) |          (static_cast<std::uint32_t>(data[2]) << 16) |          (static_cast<std::uint32_t>(data[3]) << 24);

        }

        inline std::vector<std::uint8_t> ReadBytes(     const std::vector<std::uint8_t>& line, std::uint32_t block_address,     std::uint32_t address, std::uint32_t size) {

            if (line.empty() || !RangeFitsLine(address, size, line.size()) ||       block_address != (address & ~(static_cast<std::uint32_t>(line.size()) - 1U))) {

                throw std::runtime_error("byte read exceeds cache line");

            }

            const std::size_t offset = static_cast<std::size_t>(address - block_address);

            return std::vector<std::uint8_t>(line.begin() + offset,                                    line.begin() + offset + size);

        }

        inline void WriteBytes(std::vector<std::uint8_t>* line,                        std::uint32_t block_address, std::uint32_t address,                        const std::vector<std::uint8_t>& data) {

            if (line == nullptr || line->empty() || data.empty() ||       !RangeFitsLine(address, static_cast<std::uint32_t>(data.size()),                      static_cast<std::uint32_t>(line->size())) ||       block_address !=           (address & ~(static_cast<std::uint32_t>(line->size()) - 1U))) {

                throw std::runtime_error("byte write exceeds cache line");

            }

            const std::size_t offset = static_cast<std::size_t>(address - block_address);

            for (std::size_t i = 0; i < data.size(); ++i) {

                (*line)[offset + i] = data[i];

            }

        }

        inline void CheckedIncrement(std::uint64_t* counter,                              const char* counter_name) {

            if (counter == nullptr || *counter == std::numeric_limits<std::uint64_t>::max()) {

                throw std::runtime_error(std::string("counter overflow: ") + counter_name);

            }

            ++*counter;

        }

        class TransactionIdService {

            public:   void BeginCycle() {

                planned_serials_ = serials_;

            }

            std::uint64_t Reserve(TransactionSource source) {

                const std::size_t index = static_cast<std::size_t>(source);

                if (index >= serials_.size()) {

                    throw std::runtime_error("invalid transaction-id source");

                }

                const std::uint64_t serial = planned_serials_[index];

                if (serial > (std::numeric_limits<std::uint64_t>::max() - 7U) / 8U) {

                    throw std::runtime_error("transaction-id serial overflow");

                }

                const std::uint64_t id = serial * 8U + index;

                if (id == kNoOriginId || serial == std::numeric_limits<std::uint64_t>::max()) {

                    throw std::runtime_error("transaction-id space exhausted");

                }

                planned_serials_[index] = serial + 1U;

                return id;

            }

            std::vector<std::uint64_t> ReserveBatch(TransactionSource source,                                           std::uint32_t count) {

                if (count == 0) {

                    throw std::runtime_error("zero-size transaction-id batch");

                }

                const std::size_t index = static_cast<std::size_t>(source);

                if (index >= serials_.size()) {

                    throw std::runtime_error("invalid transaction-id source");

                }

                const std::uint64_t first = planned_serials_[index];

                const std::uint64_t last = first + static_cast<std::uint64_t>(count) - 1U;

                if (last < first ||         last > (std::numeric_limits<std::uint64_t>::max() - 7U) / 8U) {

                    throw std::runtime_error("transaction-id batch exhausted");

                }

                std::vector<std::uint64_t> result;

                result.reserve(count);

                for (std::uint32_t i = 0; i < count; ++i) {

                    const std::uint64_t id = (first + i) * 8U + index;

                    if (id == kNoOriginId) {

                        throw std::runtime_error("transaction-id batch reached sentinel");

                    }

                    result.push_back(id);

                }

                planned_serials_[index] = last + 1U;

                return result;

            }

            void Commit() {

                serials_ = planned_serials_;

            }

            void Reset() {

                serials_.fill(0);

                planned_serials_.fill(0);

            }

            private:   std::array<std::uint64_t, 6> serials_ = {

            };

            std::array<std::uint64_t, 6> planned_serials_ = {

            };

        };

        // One-entry registered link with explicit operate/commit semantics. A current
        // dequeue and next enqueue may both be scheduled in the same cycle.
        template <typename T> class RegisteredLink {

            public:   void BeginCycle() {

                dequeue_planned_ = false;

                enqueue_planned_ = false;

                enqueued_value_.reset();

            }

            bool HasValue() const {

                return current_.has_value();

            }

            const T& Value() const {

                if (!current_) throw std::runtime_error("read from empty registered link");

                return *current_;

            }

            bool CanEnqueue() const {

                return !current_ || dequeue_planned_;

            }

            void PlanDequeue() {

                if (!current_ || dequeue_planned_) {

                    throw std::runtime_error("invalid registered-link dequeue");

                }

                dequeue_planned_ = true;

            }

            void PlanEnqueue(const T& value) {

                if (enqueue_planned_ || !CanEnqueue()) {

                    throw std::runtime_error("registered-link enqueue without capacity");

                }

                enqueue_planned_ = true;

                enqueued_value_ = value;

            }

            bool DequeuePlanned() const {

                return dequeue_planned_;

            }

            bool EnqueuePlanned() const {

                return enqueue_planned_;

            }

            void Commit() {

                if (dequeue_planned_) current_.reset();

                if (enqueue_planned_) current_ = std::move(enqueued_value_);

                BeginCycle();

            }

            void Reset() {

                current_.reset();

                BeginCycle();

            }

            private:   std::optional<T> current_;

            bool dequeue_planned_ = false;

            bool enqueue_planned_ = false;

            std::optional<T> enqueued_value_;

        };

        inline const char* CacheRoleName(CacheRole role) {

            switch (role) {

                case CacheRole::kL1I: return "L1I";

                case CacheRole::kL2I: return "L2I";

                case CacheRole::kL1D: return "L1D";

                case CacheRole::kL2D: return "L2D";

            }

            return "UNKNOWN";

        }

    }

    // namespace cache
}

// namespace pipesim

#endif  // PIPESIM_CACHE_CACHE_TYPES_H_
