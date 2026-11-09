#include "framework/internal.h"

namespace pipesim {

    using namespace framework_internal;

    ProcessorBuilder::ProcessorBuilder() : impl_(new Impl) {

    }

    ProcessorBuilder::~ProcessorBuilder() = default;

    NodeId ProcessorBuilder::AddStage(StageRole role, const std::string& name,                                   StageComponent* component) {

        if (component == nullptr) throw std::runtime_error("stage component is null");

        for (const StageEntry& entry : impl_->stages) {

            if (entry.component.get() == component) {

                throw std::runtime_error("stage component registered twice");

            }

        }

        const NodeId id {

            impl_->next_id++
        };

        NodeRecord node;

        node.id = id.value;

        node.kind = NodeRecord::Kind::kStage;

        node.name = name;

        node.stage_role = role;

        impl_->nodes.push_back(node);

        impl_->stages.push_back( {

            role, name, std::unique_ptr<StageComponent>(component)
        });

        return id;

    }

    NodeId ProcessorBuilder::AddRegister(RegisterRole role, const std::string& name,                                      ClockedState* state) {

        if (state == nullptr) throw std::runtime_error("clocked state is null");

        for (const RegisterEntry& entry : impl_->registers) {

            if (entry.state.get() == state) {

                throw std::runtime_error("clocked state registered twice");

            }

        }

        const NodeId id {

            impl_->next_id++
        };

        NodeRecord node;

        node.id = id.value;

        node.kind = NodeRecord::Kind::kRegister;

        node.name = name;

        node.register_role = role;

        impl_->nodes.push_back(node);

        impl_->registers.push_back( {

            role, name, std::unique_ptr<ClockedState>(state)
        });

        return id;

    }

    NodeId ProcessorBuilder::AddUnit(UnitRole role, const std::string& name,                                  TrackedUnit* unit) {

        if (unit == nullptr) throw std::runtime_error("unit is null");

        for (const UnitEntry& entry : impl_->units) {

            if (entry.unit.get() == unit) {

                throw std::runtime_error("unit registered twice");

            }

        }

        const NodeId id {

            impl_->next_id++
        };

        NodeRecord node;

        node.id = id.value;

        node.kind = NodeRecord::Kind::kUnit;

        node.name = name;

        node.unit_role = role;

        impl_->nodes.push_back(node);

        impl_->units.push_back( {

            role, name, std::unique_ptr<TrackedUnit>(unit)
        });

        return id;

    }

    NodeId ProcessorBuilder::AddCache(cache::CacheRole role,                                   const std::string& name,                                   cache::CacheController* controller) {

        if (controller == nullptr) throw std::runtime_error("cache controller is null");

        if (controller->role() != role) throw std::runtime_error("cache role mismatch");

        for (const CacheEntry& entry : impl_->caches) {

            if (entry.controller.get() == controller) {

                throw std::runtime_error("cache controller registered twice");

            }

        }

        const NodeId id {

            impl_->next_id++
        };

        NodeRecord node;

        node.id = id.value;

        node.kind = NodeRecord::Kind::kCache;

        node.name = name;

        node.cache_role = role;

        impl_->nodes.push_back(node);

        impl_->caches.push_back( {

            role, name,                            std::unique_ptr<cache::CacheController>(controller)
        });

        return id;

    }

    void ProcessorBuilder::Connect(NodeId source, NodeId destination,                                ConnectionType type) {

        if (source.value < 0 || destination.value < 0 ||       source.value >= impl_->next_id || destination.value >= impl_->next_id) {

            throw std::runtime_error("connection names an unknown node");

        }

        impl_->edges.push_back( {

            source.value, destination.value, type
        });

    }

}

// namespace pipesim
