#include "framework/internal.h"

namespace pipesim {

    using namespace framework_internal;

    std::unique_ptr<Processor> ProcessorBuilder::Build(Machine* machine) {

        if (machine == nullptr) throw std::runtime_error("builder machine is null");

        machine->impl_->RequireReady();

        const std::vector<std::string> errors = ValidateBuilder(*impl_, *machine);

        if (!errors.empty()) {

            std::ostringstream message;

            message << "invalid processor structure:";

            for (const std::string& error : errors) message << "\n  - " << error;

            throw std::runtime_error(message.str());

        }

        std::array<std::unique_ptr<cache::CacheController>, 4> controllers;

        for (CacheEntry& entry : impl_->caches) {

            controllers[static_cast<std::size_t>(entry.role)] =         std::move(entry.controller);

        }

        machine->InstallCacheControllers(       std::move(controllers[static_cast<std::size_t>(cache::CacheRole::kL1I)]),       std::move(controllers[static_cast<std::size_t>(cache::CacheRole::kL2I)]),       std::move(controllers[static_cast<std::size_t>(cache::CacheRole::kL1D)]),       std::move(controllers[static_cast<std::size_t>(cache::CacheRole::kL2D)]));

        std::unique_ptr<Processor::Impl> result(new Processor::Impl);

        result->machine = machine;

        result->nodes = std::move(impl_->nodes);

        result->edges = std::move(impl_->edges);

        result->stages = std::move(impl_->stages);

        result->registers = std::move(impl_->registers);

        result->units = std::move(impl_->units);

        return std::unique_ptr<Processor>(new Processor(std::move(result)));

    }

}

// namespace pipesim
