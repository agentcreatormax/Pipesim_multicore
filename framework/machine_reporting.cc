#include "framework/internal.h"

namespace pipesim {

    using namespace framework_internal;

    void Machine::PrintState(std::ostream& output) const {

        output << "BEGIN STATE\n";

        for (int i = 0; i < kRegisterCount; ++i) {

            output << "x" << i << " " << impl_->registers[i] << "\n";

        }

        const std::vector<std::uint8_t> bytes = impl_->ArchitecturalDataImage();

        for (std::uint32_t address = 0;        static_cast<std::uint64_t>(address) + 4 <=            impl_->config.data_memory.image_bytes;        address += 4) {

            const std::uint32_t value = static_cast<std::uint32_t>(bytes[address]) |         (static_cast<std::uint32_t>(bytes[address + 1]) << 8) |         (static_cast<std::uint32_t>(bytes[address + 2]) << 16) |         (static_cast<std::uint32_t>(bytes[address + 3]) << 24);

            if (value != 0) {

                output << "mem 0x" << std::hex << std::setw(8) << std::setfill('0')              << address << std::dec << std::setfill(' ') << " " << value              << "\n";

            }

        }

        output << "status " << (impl_->halted ? "HALTED" : "RUNNING") << "\n";

        output << "END STATE\n";

    }

    void Machine::PrintCacheState(std::ostream& output) const {

        output << "BEGIN CACHES\n";

        auto print_bytes = [&](const std::vector<std::uint8_t>& bytes) {

            if (bytes.empty()) {

                output << "-";

                return;

            }

            for (std::uint8_t byte : bytes) {

                output << std::hex << std::setw(2) << std::setfill('0')              << static_cast<unsigned>(byte);

            }

            output << std::dec << std::setfill(' ');

        };

        const cache::CacheController* controllers[] = {

            impl_->l1i.get(), impl_->l2i.get(), impl_->l1d.get(), impl_->l2d.get()
        };

        const char* stages[] = {

            "INDEX", "LOOKUP", "OUTPUT"
        };

        for (const cache::CacheController* controller : controllers) {

            const cache::CacheSnapshot snapshot = controller->Snapshot();

            output << "cache " << cache::CacheRoleName(controller->role()) << "\n";

            for (std::size_t i = 0; i < snapshot.pipeline.size(); ++i) {

                output << "  " << stages[i] << " "              << (snapshot.pipeline[i].valid ? "VALID" : "EMPTY");

                if (snapshot.pipeline[i].valid) {

                    output << " id " << snapshot.pipeline[i].request_id << " address 0x"                << std::hex << snapshot.pipeline[i].address << std::dec;

                }

                output << "\n";

            }

            for (std::size_t slot = 0; slot < snapshot.mshrs.size(); ++slot) {

                const cache::MshrSnapshot& entry = snapshot.mshrs[slot];

                output << "  MSHR " << slot << " "              << (entry.valid ? "VALID" : "EMPTY") << " block 0x"              << std::hex << entry.block_address << std::dec << " set "              << entry.set << " tag " << entry.tag << " way "              << entry.reserved_way << " lower " << entry.lower_id              << " order " << entry.arrival_order << " state "              << (entry.state.empty() ? "-" : entry.state) << " waiters "              << entry.waiters.size() << "/" << entry.waiter_capacity << "\n";

                for (std::size_t index = 0; index < entry.waiters.size(); ++index) {

                    const cache::MshrWaiter& waiter = entry.waiters[index];

                    output << "    waiter " << index << " id " << waiter.request_id                << " origin " << waiter.origin_id << " sequence "                << waiter.sequence << " order " << waiter.arrival_order                << " generation " << waiter.generation << " type "                << static_cast<int>(waiter.type) << " address 0x" << std::hex                << waiter.address << std::dec << " size " << waiter.size                << " ready " << (waiter.response_ready ? 1 : 0)                << " sent " << (waiter.response_sent ? 1 : 0) << " data ";

                    print_bytes(waiter.data);

                    output << " response ";

                    print_bytes(waiter.response_data);

                    output << "\n";

                }

            }

            for (std::size_t slot = 0; slot < snapshot.writebacks.size(); ++slot) {

                const cache::WritebackSnapshot& entry = snapshot.writebacks[slot];

                output << "  WB " << slot << " "              << (entry.valid ? "VALID" : "EMPTY") << " address 0x"              << std::hex << entry.address << std::dec << " size "              << entry.size << " lower " << entry.lower_id << " order "              << entry.arrival_order << " bypasses " << entry.demand_bypasses              << " state " << (entry.state.empty() ? "-" : entry.state)              << " waiters " << entry.waiters.size() << "/"              << entry.waiter_capacity << " data ";

                print_bytes(entry.data);

                output << "\n";

                for (std::size_t index = 0; index < entry.waiters.size(); ++index) {

                    const cache::WritebackAckWaiter& waiter = entry.waiters[index];

                    output << "    ack " << index << " id " << waiter.request_id                << " origin " << waiter.origin_id << " sequence "                << waiter.sequence << " order " << waiter.arrival_order                << " generation " << waiter.generation << " address 0x"                << std::hex << waiter.address << std::dec << " size "                << waiter.size << " sent "                << (waiter.response_sent ? 1 : 0) << "\n";

                }

            }

            for (const cache::LineSnapshot& entry : snapshot.lines) {

                output << "  LINE set " << entry.set << " way " << entry.way              << " valid " << (entry.line.valid ? 1 : 0) << " dirty "              << (entry.line.dirty ? 1 : 0) << " reserved "              << (entry.line.reserved ? 1 : 0) << " tag " << entry.line.tag              << " owner " << entry.reservation_owner << " data ";

                print_bytes(entry.line.data);

                output << "\n";

            }

            for (const cache::ReplacementSnapshot& entry : snapshot.replacement) {

                output << "  REPLACEMENT set " << entry.set << " state";

                for (std::uint32_t value : entry.state) output << " " << value;

                output << "\n";

            }

        }

        auto print_request_link = [&](const char* name, const auto& link) {

            output << "link " << name << " ";

            if (!link.HasValue()) {

                output << "EMPTY\n";

                return;

            }

            const cache::CacheRequest& request = link.Value();

            output << "VALID id " << request.id << " origin " << request.origin_id            << " sequence " << request.sequence << " generation "            << request.generation << " address 0x" << std::hex            << request.address << std::dec << " size " << request.size            << " type " << static_cast<int>(request.type) << " data ";

            print_bytes(request.data);

            output << "\n";

        };

        auto print_response_link = [&](const char* name, const auto& link) {

            output << "link " << name << " ";

            if (!link.HasValue()) {

                output << "EMPTY\n";

                return;

            }

            const cache::CacheResponse& response = link.Value();

            output << "VALID id " << response.id << " origin " << response.origin_id            << " sequence " << response.sequence << " generation "            << response.generation << " address 0x" << std::hex            << response.address << std::dec << " size " << response.size            << " type " << static_cast<int>(response.type) << " data ";

            print_bytes(response.data);

            output << "\n";

        };

        print_request_link("CPU_L1I_REQUEST", impl_->cpu_l1i_request);

        print_request_link("L1I_L2I_REQUEST", impl_->l1i_l2i_request);

        print_request_link("L2I_MEMORY_REQUEST", impl_->l2i_memory_request);

        print_response_link("L1I_CPU_RESPONSE", impl_->l1i_cpu_response);

        print_response_link("L2I_L1I_RESPONSE", impl_->l2i_l1i_response);

        print_response_link("MEMORY_L2I_RESPONSE", impl_->memory_l2i_response);

        print_request_link("CPU_L1D_REQUEST", impl_->cpu_l1d_request);

        print_request_link("L1D_L2D_REQUEST", impl_->l1d_l2d_request);

        print_request_link("L2D_MEMORY_REQUEST", impl_->l2d_memory_request);

        print_response_link("L1D_CPU_RESPONSE", impl_->l1d_cpu_response);

        print_response_link("L2D_L1D_RESPONSE", impl_->l2d_l1d_response);

        print_response_link("MEMORY_L2D_RESPONSE", impl_->memory_l2d_response);

        auto print_memory = [&](const char* name,                           const framework::SimpleMemory* memory) {

            const std::vector<framework::MemoryOutstandingSnapshot> entries =         memory->Snapshot();

            output << "memory " << name << " outstanding " << entries.size()            << "\n";

            for (std::size_t index = 0; index < entries.size(); ++index) {

                const framework::MemoryOutstandingSnapshot& entry = entries[index];

                output << "  slot " << index << " id " << entry.request_id              << " address 0x" << std::hex << entry.address << std::dec              << " size " << entry.size << " accepted "              << entry.accepted_cycle << " ready " << entry.ready_cycle              << " materialized " << (entry.materialized ? 1 : 0) << "\n";

            }

        };

        print_memory("InstructionMemory", impl_->instruction_memory.get());

        print_memory("DataMemory", impl_->data_memory.get());

        output << "END CACHES\n";

    }

    void Machine::PrintStatistics(std::ostream& output) const {

        output << "icache_input_stalls " << impl_->icache_input_stalls << "\n";

        output << "dcache_input_stalls " << impl_->dcache_input_stalls << "\n";

        output << "outstanding_raw_stalls " << impl_->outstanding_raw_stalls << "\n";

        output << "outstanding_waw_stalls " << impl_->outstanding_waw_stalls << "\n";

        output << "fetch_queue_full_stalls " << impl_->fetch_queue_full_stalls << "\n";

        output << "pending_data_full_stalls " << impl_->pending_data_full_stalls << "\n";

        output << "completion_queue_full_stalls " << impl_->completion_queue_full_stalls << "\n";

        output << "execution_unit_busy_stalls " << impl_->execution_unit_busy_stalls << "\n";

        output << "execution_completion_slot_stalls "          << impl_->execution_completion_slot_stalls << "\n";

        output << "stale_ifetch_responses " << impl_->stale_ifetch_responses << "\n";

        const cache::CacheController* controllers[] = {

            impl_->l1i.get(), impl_->l2i.get(), impl_->l1d.get(), impl_->l2d.get()
        };

        for (const cache::CacheController* controller : controllers) {

            const cache::CacheStats& stats = controller->stats();

            const std::string prefix = std::string(cache::CacheRoleName(controller->role())) + ".";

            output << prefix << "demand_accesses_accepted " << stats.demand_accesses_accepted << "\n";

            output << prefix << "writeback_requests_accepted " << stats.writeback_requests_accepted << "\n";

            output << prefix << "hits " << stats.hits << "\n";

            output << prefix << "writeback_hits " << stats.writeback_hits << "\n";

            output << prefix << "primary_misses " << stats.primary_misses << "\n";

            output << prefix << "mshr_merges " << stats.mshr_merges << "\n";

            output << prefix << "mshr_full_stalls " << stats.mshr_full_stalls << "\n";

            output << prefix << "mshr_waiter_full_stalls " << stats.mshr_waiter_full_stalls << "\n";

            output << prefix << "reserved_way_stalls " << stats.reserved_way_stalls << "\n";

            output << prefix << "writeback_full_stalls " << stats.writeback_full_stalls << "\n";

            output << prefix << "writeback_waiter_full_stalls " << stats.writeback_waiter_full_stalls << "\n";

            output << prefix << "fills " << stats.fills << "\n";

            output << prefix << "evictions " << stats.evictions << "\n";

            output << prefix << "dirty_evictions " << stats.dirty_evictions << "\n";

            output << prefix << "writebacks_generated " << stats.writebacks_generated << "\n";

            output << prefix << "writebacks_sent " << stats.writebacks_sent << "\n";

            output << prefix << "writeback_merges " << stats.writeback_merges << "\n";

            output << prefix << "writeback_conflict_stalls " << stats.writeback_conflict_stalls << "\n";

            output << prefix << "writeback_order_stalls " << stats.writeback_order_stalls << "\n";

            output << prefix << "input_queue_stalls " << stats.input_queue_stalls << "\n";

            output << prefix << "output_queue_stalls " << stats.output_queue_stalls << "\n";

        }

    }

}

// namespace pipesim
