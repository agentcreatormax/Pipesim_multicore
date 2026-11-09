#ifndef PIPESIM_FRAMEWORK_SIMPLE_MEMORY_H_
#define PIPESIM_FRAMEWORK_SIMPLE_MEMORY_H_

#include "cache/cache_config.h"
#include "cache/cache_types.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace pipesim {

    namespace framework {

        class ByteImage {

            public:   ByteImage() = default;

            ByteImage(std::uint32_t logical_size, std::uint32_t padding_alignment);

            void Configure(std::uint32_t logical_size,                  std::uint32_t padding_alignment);

            void SetInitialWord(std::uint32_t address, std::uint32_t value);

            void SealInitialImage();

            void Reset();

            std::vector<std::uint8_t> Read(std::uint32_t address,                                  std::uint32_t size) const;

            void Write(std::uint32_t address, const std::vector<std::uint8_t>& data);

            std::uint32_t logical_size() const {

                return logical_size_;

            }

            std::uint32_t padded_size() const {

                return padded_size_;

            }

            const std::vector<std::uint8_t>& bytes() const {

                return current_;

            }

            private:   void CheckRange(std::uint32_t address, std::uint32_t size) const;

            std::uint32_t logical_size_ = 0;

            std::uint32_t padded_size_ = 0;

            std::vector<std::uint8_t> initial_;

            std::vector<std::uint8_t> current_;

        };

        struct MemoryOutstandingSnapshot {

            std::uint64_t request_id = 0;

            std::uint32_t address = 0;

            std::uint32_t size = 0;

            std::uint64_t accepted_cycle = 0;

            std::uint64_t ready_cycle = 0;

            bool materialized = false;

        };

        struct MemoryIntent {

            bool can_accept_request = false;

            std::optional<cache::CacheResponse> response;

        };

        class SimpleMemory {

            public:   SimpleMemory(std::string name, bool instruction_side,                cache::MemoryConfig config, ByteImage* image);

            void Reset();

            void BeginCycle();

            MemoryIntent Inspect(std::uint64_t current_cycle) const;

            void Operate(std::uint64_t current_cycle,                const cache::CacheRequest* accepted_request,                bool response_granted);

            void Commit();

            bool HasWork() const {

                return !current_.empty();

            }

            std::optional<std::uint64_t> NextReadyCycle() const;

            std::vector<MemoryOutstandingSnapshot> Snapshot() const;

            private:   struct Outstanding {

                cache::CacheRequest request;

                std::uint64_t accepted_cycle = 0;

                std::uint64_t ready_cycle = 0;

                bool materialized = false;

                cache::CacheResponse response;

            };

            cache::CacheResponse Materialize(const Outstanding& entry) const;

            void ValidateRequest(const cache::CacheRequest& request) const;

            std::string name_;

            bool instruction_side_ = false;

            cache::MemoryConfig config_;

            ByteImage* image_ = nullptr;

            std::deque<Outstanding> current_;

            std::deque<Outstanding> next_;

            std::optional<std::pair<std::uint32_t, std::vector<std::uint8_t>>>       pending_write_;

        };

    }

    // namespace framework
}

// namespace pipesim

#endif  // PIPESIM_FRAMEWORK_SIMPLE_MEMORY_H_
