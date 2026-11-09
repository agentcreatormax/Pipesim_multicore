#ifndef PIPESIM_CACHE_CACHE_CONTROLLER_H_
#define PIPESIM_CACHE_CACHE_CONTROLLER_H_

#include "cache/cache_config.h"
#include "cache/cache_types.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace pipesim {

    namespace cache {

        struct CacheStageIntent {

            bool accept_upper_request = false;

            bool accept_lower_response = false;

        };

        struct CacheOperateGrants {

            bool upper_request = false;

            bool lower_response = false;

            bool upper_response = false;

            bool lower_request = false;

            bool lower_request_queue_available = false;

        };

        // Generic cache controller. One class is instantiated as L1I, L2I,
        // L1D, and L2D. The public operate/commit API is frozen.
        class CacheController {

            public:   CacheController(CacheRole role, const CacheConfig& config,                   TransactionIdService* transaction_ids);

            ~CacheController();

            CacheController(const CacheController&) = delete;

            CacheController& operator=(const CacheController&) = delete;

            void Reset();

            void BeginCycle();

            std::optional<CacheResponse> ResponseIntent() const;

            CacheStageIntent StageIntent(const CacheRequest* upper_request,                                const CacheResponse* lower_response,                                bool upper_response_granted,                                bool lower_request_granted) const;

            std::optional<CacheRequest> LowerRequestIntent() const;

            void Operate(std::uint64_t current_cycle,                const CacheRequest* upper_request,                const CacheResponse* lower_response,                const CacheOperateGrants& grants);

            void Commit();

            CacheRole role() const;

            const CacheConfig& config() const;

            const CacheStats& stats() const;

            CacheSnapshot Snapshot() const;

            bool HasWork() const;

            private:   class Impl;

            std::unique_ptr<Impl> impl_;

        };

    }

    // namespace cache
}

// namespace pipesim

#endif  // PIPESIM_CACHE_CACHE_CONTROLLER_H_
