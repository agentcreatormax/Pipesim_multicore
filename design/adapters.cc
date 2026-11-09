#include "design/adapters.h"
namespace pipesim::design_internal {

    void StageAdapter::Reset() {

        core_->ResetStage(role_);

    }

    void StageAdapter::Evaluate() {

        switch(role_) {

            case StageRole::kFetch:core_->Fetch();

            break;

            case StageRole::kDecode:core_->Decode();

            break;

            case StageRole::kExecute:core_->Execute();

            break;

            case StageRole::kMemory:core_->Memory();

            break;

            case StageRole::kWriteback:core_->Writeback();

            break;

        }

    }

    StageSnapshot StageAdapter::Snapshot()const {

        return core_->Snapshot(role_);

    }

    void RegisterAdapter::Reset() {

        core_->ResetRegister(kind_);

    }

    void RegisterAdapter::Commit() {

        core_->CommitRegister(kind_);

    }

    std::string RegisterAdapter::Snapshot()const {

        return core_->RegisterSnapshot(kind_);

    }

}

