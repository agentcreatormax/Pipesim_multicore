#pragma once
#include "pipesim.h"
#include "design/core.h"
#include <memory>
namespace pipesim::design_internal {

    class StageAdapter final : public StageComponent {

        public: StageAdapter(std::shared_ptr<Core> core,StageRole role):core_(std::move(core)),role_(role) {

        }

        void Reset() override;

        void Evaluate() override;

        StageSnapshot Snapshot() const override;

        private: std::shared_ptr<Core> core_;

        StageRole role_;

    };

    class RegisterAdapter final : public ClockedState {

        public: RegisterAdapter(std::shared_ptr<Core> core,RegisterKind kind):core_(std::move(core)),kind_(kind) {

        }

        void Reset() override;

        void Commit() override;

        std::string Snapshot() const override;

        private: std::shared_ptr<Core> core_;

        RegisterKind kind_;

    };

}

