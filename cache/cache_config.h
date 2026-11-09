#ifndef PIPESIM_CACHE_CACHE_CONFIG_H_
#define PIPESIM_CACHE_CACHE_CONFIG_H_

#include "execution_config.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pipesim {

    namespace cache {

        enum class ReplacementKind {

            kTreePlru,   kSrrip,
        };

        enum class FillMode {

            kCompleteLine,
        };

        struct CacheConfig {

            std::uint32_t line_size = 0;

            std::uint32_t sets = 0;

            std::uint32_t ways = 0;

            std::uint32_t mshr_entries = 0;

            std::uint32_t mshr_waiters_per_entry = 0;

            ReplacementKind replacement = ReplacementKind::kTreePlru;

            std::uint32_t writeback_entries = 0;

            std::uint32_t writeback_waiters_per_entry = 0;

            FillMode fill_mode = FillMode::kCompleteLine;

        };

        struct CpuConfig {

            std::uint32_t fetch_queue_entries = 0;

            std::uint32_t pending_data_entries = 0;

            std::uint32_t completion_queue_entries = 0;

        };

        struct MemoryConfig {

            std::uint32_t latency = 0;

            std::uint32_t max_outstanding = 0;

            std::uint32_t image_bytes = 0;

        };

        struct SystemConfig {

            CacheConfig l1i;

            CacheConfig l2i;

            CacheConfig l1d;

            CacheConfig l2d;

            CpuConfig cpu;

            std::array<::pipesim::ExecutionUnitConfig, 7> execution_units = {

            };

            MemoryConfig instruction_memory;

            MemoryConfig data_memory;

        };

        bool LoadSystemConfig(const std::string& file_name, SystemConfig* config,                       std::string* error);

        const char* ReplacementKindName(ReplacementKind replacement);

    }

    // namespace cache
}

// namespace pipesim

#endif  // PIPESIM_CACHE_CACHE_CONFIG_H_
