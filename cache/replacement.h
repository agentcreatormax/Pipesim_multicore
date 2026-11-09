#ifndef PIPESIM_CACHE_REPLACEMENT_H_
#define PIPESIM_CACHE_REPLACEMENT_H_

#include "cache/cache_config.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace pipesim {

    namespace cache {

        class ReplacementPolicy {

            public:   virtual ~ReplacementPolicy() = default;

            virtual void Reset() = 0;

            virtual void BeginCycle() = 0;

            virtual void OnHit(std::uint32_t set, std::uint32_t way) = 0;

            virtual void OnInsert(std::uint32_t set, std::uint32_t way) = 0;

            virtual int PeekVictim(std::uint32_t set,                          const std::vector<bool>& eligible) const = 0;

            virtual int SelectVictim(std::uint32_t set,                            const std::vector<bool>& eligible) = 0;

            virtual std::vector<std::uint32_t> DebugState(std::uint32_t set) const = 0;

            virtual void Commit() = 0;

        };

        std::unique_ptr<ReplacementPolicy> MakeReplacementPolicy(     ReplacementKind kind, std::uint32_t sets, std::uint32_t ways);

    }

    // namespace cache
}

// namespace pipesim

#endif  // PIPESIM_CACHE_REPLACEMENT_H_
