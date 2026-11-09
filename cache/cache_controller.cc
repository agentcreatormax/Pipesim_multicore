#include "cache/cache_controller/internal.h"

namespace pipesim::cache {

    CacheController::CacheController(CacheRole role, const CacheConfig& config,                                  TransactionIdService* transaction_ids)     : impl_(new Impl(role, config, transaction_ids)) {

    }

    CacheController::~CacheController() = default;

    void CacheController::Reset() {

        impl_->Reset();

    }

    void CacheController::BeginCycle() {

        impl_->BeginCycle();

    }

    std::optional<CacheResponse> CacheController::ResponseIntent() const {

        return impl_->ResponseIntent();

    }

    CacheStageIntent CacheController::StageIntent(     const CacheRequest* upper_request,     const CacheResponse* lower_response,     bool upper_response_granted,     bool lower_request_granted) const {

        return impl_->StageIntent(upper_request, lower_response,                             upper_response_granted,                             lower_request_granted);

    }

    std::optional<CacheRequest> CacheController::LowerRequestIntent() const {

        return impl_->LowerRequestIntent();

    }

    void CacheController::Operate(std::uint64_t current_cycle,                               const CacheRequest* upper_request,                               const CacheResponse* lower_response,                               const CacheOperateGrants& grants) {

        impl_->Operate(current_cycle, upper_request, lower_response, grants);

    }

    void CacheController::Commit() {

        impl_->Commit();

    }

    CacheRole CacheController::role() const {

        return impl_->role_;

    }

    const CacheConfig& CacheController::config() const {

        return impl_->config_;

    }

    const CacheStats& CacheController::stats() const {

        return impl_->stats_;

    }

    CacheSnapshot CacheController::Snapshot() const {

        return impl_->Snapshot();

    }

    bool CacheController::HasWork() const {

        return impl_->HasWork();

    }

}

