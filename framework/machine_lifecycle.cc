#include "framework/internal.h"

namespace pipesim {

    using namespace framework_internal;

    Machine::Machine() : impl_(new Impl) {

    }

    Machine::~Machine() = default;

    bool Machine::LoadConfiguration(const std::string& file_name,                                 std::string* error) {

        cache::SystemConfig parsed;

        if (!cache::LoadSystemConfig(file_name, &parsed, error)) return false;

        try {

            impl_->config = parsed;

            impl_->instruction_image.Configure(parsed.instruction_memory.image_bytes,                                        parsed.l2i.line_size);

            impl_->data_image.Configure(parsed.data_memory.image_bytes,                                 parsed.l2d.line_size);

            impl_->instruction_memory.reset(new framework::SimpleMemory(         "InstructionMemory", true, parsed.instruction_memory,         &impl_->instruction_image));

            impl_->data_memory.reset(new framework::SimpleMemory(         "DataMemory", false, parsed.data_memory, &impl_->data_image));

            impl_->configured = true;

            impl_->program_loaded = false;

            return true;

        }

        catch (const std::exception& exception) {

            if (error != nullptr) *error = exception.what();

            return false;

        }

    }

    bool Machine::LoadProgram(const std::string& file_name, std::string* error) {

        if (!impl_->configured) {

            if (error != nullptr) *error = "load JSON configuration before the program";

            return false;

        }

        impl_->program_loaded = false;

        std::ifstream input(file_name);

        if (!input) {

            if (error != nullptr) *error = "cannot open program file";

            return false;

        }

        impl_->instruction_image.Configure(       impl_->config.instruction_memory.image_bytes, impl_->config.l2i.line_size);

        impl_->data_image.Configure(impl_->config.data_memory.image_bytes,                               impl_->config.l2d.line_size);

        impl_->initial_registers.fill(0);

        impl_->token_to_instruction.clear();

        impl_->instruction_pcs.clear();

        impl_->entry_point = 0;

        bool saw_header = false;

        bool saw_entry = false;

        bool saw_end = false;

        std::set<int> register_records;

        std::set<std::uint32_t> memory_records;

        std::set<std::uint32_t> instruction_records;

        std::string line;

        int line_number = 0;

        std::uint32_t next_token = 1;

        auto fail = [&](const std::string& message) {

            if (error != nullptr) {

                *error = "line " + std::to_string(line_number) + ": " + message;

            }

            return false;

        };

        try {

            while (std::getline(input, line)) {

                ++line_number;

                const std::size_t comment = line.find('#');

                if (comment != std::string::npos) line = line.substr(0, comment);

                line = Trim(line);

                if (line.empty()) continue;

                if (saw_end) return fail("record appears after END");

                std::istringstream fields(line);

                std::string kind;

                fields >> kind;

                if (kind == "PIPEISA") {

                    int version = 0;

                    if (saw_header || !(fields >> version) || version != 1 ||             HasExtra(&fields)) {

                        return fail("expected exactly one PIPEISA 1 header");

                    }

                    saw_header = true;

                    continue;

                }

                if (!saw_header) return fail("missing PIPEISA 1 header");

                if (kind == "ENTRY") {

                    std::string value;

                    if (saw_entry || !(fields >> value) ||             !ParseUnsigned(value, &impl_->entry_point) || HasExtra(&fields) ||             impl_->entry_point % 4 != 0) {

                        return fail("invalid or duplicate ENTRY record");

                    }

                    saw_entry = true;

                }

                else if (kind == "REG") {

                    int index = -1;

                    std::string value_text;

                    std::uint32_t value = 0;

                    if (!(fields >> index >> value_text) || HasExtra(&fields) ||             index < 0 || index >= kRegisterCount ||             !ParseUnsigned(value_text, &value) ||             !register_records.insert(index).second) {

                        return fail("invalid or duplicate REG record");

                    }

                    if (index != 0) impl_->initial_registers[index] = value;

                }

                else if (kind == "MEM") {

                    std::string address_text;

                    std::string value_text;

                    std::uint32_t address = 0;

                    std::uint32_t value = 0;

                    if (!(fields >> address_text >> value_text) || HasExtra(&fields) ||             !ParseUnsigned(address_text, &address) ||             !ParseUnsigned(value_text, &value) || address % 4 != 0 ||             static_cast<std::uint64_t>(address) + 4 >                 impl_->config.data_memory.image_bytes ||             !memory_records.insert(address).second) {

                        return fail("invalid, out-of-range, or duplicate MEM record");

                    }

                    impl_->data_image.SetInitialWord(address, value);

                }

                else if (kind == "INST") {

                    std::string pc_text;

                    std::string opcode_text;

                    std::string immediate_text;

                    Instruction instruction;

                    if (!(fields >> pc_text >> opcode_text >> instruction.rd >>               instruction.rs1 >> instruction.rs2 >> immediate_text) ||             HasExtra(&fields) || !ParseUnsigned(pc_text, &instruction.pc) ||             !ParseOpcode(opcode_text, &instruction.opcode) ||             !ParseSigned(immediate_text, &instruction.immediate) ||             instruction.pc % 4 != 0 || instruction.rd < 0 ||             instruction.rd >= kRegisterCount || instruction.rs1 < 0 ||             instruction.rs1 >= kRegisterCount || instruction.rs2 < 0 ||             instruction.rs2 >= kRegisterCount ||             static_cast<std::uint64_t>(instruction.pc) + 4 >                 impl_->config.instruction_memory.image_bytes ||             !instruction_records.insert(instruction.pc).second) {

                        return fail("invalid, out-of-range, or duplicate INST record");

                    }

                    bool canonical = instruction.opcode != Opcode::kNop;

                    switch (instruction.opcode) {

                        case Opcode::kAdd:           case Opcode::kSub:           case Opcode::kAnd:           case Opcode::kOr:           case Opcode::kXor:             canonical = canonical && instruction.immediate == 0;

                        break;

                        case Opcode::kAddi:           case Opcode::kLw:             canonical = canonical && instruction.rs2 == 0;

                        break;

                        case Opcode::kSw:             canonical = canonical && instruction.rd == 0;

                        break;

                        case Opcode::kBeq:           case Opcode::kBne:             canonical = canonical && instruction.rd == 0;

                        break;

                        case Opcode::kJ:             canonical = canonical && instruction.rd == 0 &&                         instruction.rs1 == 0 && instruction.rs2 == 0;

                        break;

                        case Opcode::kXchg:             break;

                        case Opcode::kFence:           case Opcode::kHalt:             canonical = canonical && instruction.rd == 0 &&                         instruction.rs1 == 0 && instruction.rs2 == 0 &&                         instruction.immediate == 0;

                        break;

                        case Opcode::kNop:             canonical = false;

                        break;

                    }

                    if (!canonical) return fail("non-canonical or reserved instruction");

                    if (next_token == 0) return fail("instruction-token space exhausted");

                    impl_->token_to_instruction.emplace(next_token, instruction);

                    impl_->instruction_image.SetInitialWord(instruction.pc, next_token);

                    ++next_token;

                }

                else if (kind == "END") {

                    if (HasExtra(&fields)) return fail("END takes no operands");

                    saw_end = true;

                }

                else {

                    return fail("unknown record type " + kind);

                }

            }

            if (!saw_header || !saw_entry || !saw_end) {

                if (error != nullptr) {

                    *error = "program requires PIPEISA 1, one ENTRY, and END";

                }

                return false;

            }

            if (instruction_records.count(impl_->entry_point) == 0) {

                if (error != nullptr) *error = "ENTRY does not name an INST address";

                return false;

            }

            for (const auto& item : impl_->token_to_instruction) {

                const Instruction& instruction = item.second;

                if (instruction.opcode != Opcode::kBeq &&           instruction.opcode != Opcode::kBne &&           instruction.opcode != Opcode::kJ) {

                    continue;

                }

                const std::uint32_t target =           static_cast<std::uint32_t>(instruction.immediate);

                if (target % 4 != 0 ||           static_cast<std::uint64_t>(target) + 4 >               impl_->config.instruction_memory.image_bytes ||           instruction_records.count(target) == 0) {

                    if (error != nullptr) *error = "control target does not name an INST";

                    return false;

                }

            }

            impl_->instruction_pcs = instruction_records;

            impl_->instruction_image.SealInitialImage();

            impl_->data_image.SealInitialImage();

            impl_->program_loaded = true;

            Reset();

            return true;

        }

        catch (const std::exception& exception) {

            if (error != nullptr) *error = exception.what();

            return false;

        }

    }

    void Machine::InstallCacheControllers(     std::unique_ptr<cache::CacheController> l1i,     std::unique_ptr<cache::CacheController> l2i,     std::unique_ptr<cache::CacheController> l1d,     std::unique_ptr<cache::CacheController> l2d) {

        if (!l1i || !l2i || !l1d || !l2d) {

            throw std::runtime_error("builder did not supply four cache controllers");

        }

        impl_->l1i = std::move(l1i);

        impl_->l2i = std::move(l2i);

        impl_->l1d = std::move(l1d);

        impl_->l2d = std::move(l2d);

    }

    void Machine::Reset() {

        impl_->RequireReady();

        impl_->registers = impl_->initial_registers;

        impl_->registers[0] = 0;

        impl_->instruction_image.Reset();

        impl_->data_image.Reset();

        impl_->transaction_ids.Reset();

        impl_->ResetLinks();

        if (impl_->l1i) impl_->l1i->Reset();

        if (impl_->l2i) impl_->l2i->Reset();

        if (impl_->l1d) impl_->l1d->Reset();

        if (impl_->l2d) impl_->l2d->Reset();

        impl_->instruction_memory->Reset();

        impl_->data_memory->Reset();

        impl_->fetch_current.assign(impl_->config.cpu.fetch_queue_entries,                               Impl::FetchSlot());

        impl_->fetch_next = impl_->fetch_current;

        impl_->data_current.assign(impl_->config.cpu.pending_data_entries,                              Impl::PendingData());

        impl_->data_next = impl_->data_current;

        impl_->completion_current.clear();

        impl_->completion_next.clear();

        std::uint32_t maximum_fixed_latency = 0;

        for (std::size_t index = 0;        index < static_cast<std::size_t>(ExecutionUnitKind::kAgu); ++index) {

            maximum_fixed_latency = std::max(         maximum_fixed_latency, impl_->config.execution_units[index].latency);

        }

        const std::uint64_t calendar_size =       static_cast<std::uint64_t>(maximum_fixed_latency) + 1U;

        if (calendar_size > impl_->reservation_current.max_size()) {

            throw std::runtime_error("completion reservation calendar is too large");

        }

        impl_->reservation_current.assign(       static_cast<std::size_t>(calendar_size), std::nullopt);

        impl_->reservation_next = impl_->reservation_current;

        impl_->current_boundary = 0;

        impl_->next_fetch_sequence_current = 0;

        impl_->next_fetch_sequence_next = 0;

        impl_->fetch_generation_current = 0;

        impl_->fetch_generation_next = 0;

        impl_->issued_fetch_this_cycle = false;

        impl_->issued_data_this_cycle = false;

        impl_->retirement_planned = false;

        impl_->normal_completion_seen = false;

        impl_->stall_point_used.fill(false);

        impl_->data_stall_recorded = false;

        impl_->halted = false;

        impl_->retired = 0;

        impl_->data_stalls = 0;

        impl_->control_stalls = 0;

        impl_->icache_input_stalls = 0;

        impl_->dcache_input_stalls = 0;

        impl_->outstanding_raw_stalls = 0;

        impl_->outstanding_waw_stalls = 0;

        impl_->fetch_queue_full_stalls = 0;

        impl_->pending_data_full_stalls = 0;

        impl_->completion_queue_full_stalls = 0;

        impl_->execution_unit_busy_stalls = 0;

        impl_->execution_completion_slot_stalls = 0;

        impl_->stale_ifetch_responses = 0;

    }

    void Machine::BeginCycle(std::uint64_t cycle) {

        if (!impl_->l1i || !impl_->l2i || !impl_->l1d || !impl_->l2d) {

            throw std::runtime_error("cache hierarchy is not installed");

        }

        if (cycle != impl_->current_boundary) {

            throw std::runtime_error("processor/cache boundary counter mismatch");

        }

        impl_->fetch_next = impl_->fetch_current;

        impl_->data_next = impl_->data_current;

        impl_->completion_next = impl_->completion_current;

        impl_->reservation_next.assign(impl_->reservation_current.size(),                                  std::nullopt);

        for (std::size_t index = 0;        index + 1U < impl_->reservation_current.size(); ++index) {

            impl_->reservation_next[index] = impl_->reservation_current[index + 1U];

        }

        impl_->next_fetch_sequence_next = impl_->next_fetch_sequence_current;

        impl_->fetch_generation_next = impl_->fetch_generation_current;

        impl_->issued_fetch_this_cycle = false;

        impl_->issued_data_this_cycle = false;

        impl_->retirement_planned = false;

        impl_->normal_completion_seen = false;

        impl_->stall_point_used.fill(false);

        impl_->data_stall_recorded = false;

        impl_->transaction_ids.BeginCycle();

        impl_->BeginLinks();

        impl_->l1i->BeginCycle();

        impl_->l2i->BeginCycle();

        if(!impl_->external_submit) {

            impl_->l1d->BeginCycle();

            impl_->l2d->BeginCycle();

        }

        impl_->instruction_memory->BeginCycle();

        if(!impl_->external_submit) impl_->data_memory->BeginCycle();

        impl_->ConsumeInstructionResponse();

        if (impl_->external_poll) {

            auto r=impl_->external_poll();

            if(r) {

                auto* p=impl_->FindData(&impl_->data_next,r->first);

                if(!p) throw std::runtime_error("external reply has no owner");

                auto* c=impl_->FindCompletion(&impl_->completion_next,p->sequence);

                if(!c || c->ready) throw std::runtime_error("duplicate external completion");

                c->value=r->second;

                c->ready=true;

                p->valid=false;

            }

        }

        else impl_->ConsumeDataResponse();

        impl_->OperateSide(cycle, impl_->l1i.get(), impl_->l2i.get(),                      impl_->instruction_memory.get(),                      &impl_->cpu_l1i_request, &impl_->l1i_l2i_request,                      &impl_->l2i_memory_request, &impl_->l1i_cpu_response,                      &impl_->l2i_l1i_response, &impl_->memory_l2i_response);

        if (!impl_->external_submit) impl_->OperateSide(cycle, impl_->l1d.get(), impl_->l2d.get(),                      impl_->data_memory.get(),                      &impl_->cpu_l1d_request, &impl_->l1d_l2d_request,                      &impl_->l2d_memory_request, &impl_->l1d_cpu_response,                      &impl_->l2d_l1d_response, &impl_->memory_l2d_response);

    }

    void Machine::EndCycle(std::uint64_t cycle) {

        if (cycle != impl_->current_boundary) {

            throw std::runtime_error("end-cycle boundary mismatch");

        }

        const bool reservation_due =       !impl_->reservation_current.empty() &&       impl_->reservation_current[0].has_value();

        if (reservation_due != impl_->normal_completion_seen) {

            throw std::runtime_error(         "completion reservation has no matching fixed-unit output");

        }

        if (impl_->current_boundary ==       std::numeric_limits<std::uint64_t>::max()) {

            throw std::runtime_error("global boundary counter overflow");

        }

    }

    void Machine::CommitCycle() {

        impl_->l1i->Commit();

        impl_->l2i->Commit();

        if(!impl_->external_submit) {

            impl_->l1d->Commit();

            impl_->l2d->Commit();

        }

        impl_->instruction_memory->Commit();

        if(!impl_->external_submit) impl_->data_memory->Commit();

        impl_->CommitLinks();

        impl_->transaction_ids.Commit();

        impl_->fetch_current = std::move(impl_->fetch_next);

        impl_->data_current = std::move(impl_->data_next);

        impl_->completion_current = std::move(impl_->completion_next);

        impl_->reservation_current = std::move(impl_->reservation_next);

        impl_->next_fetch_sequence_current = impl_->next_fetch_sequence_next;

        impl_->fetch_generation_current = impl_->fetch_generation_next;

        ++impl_->current_boundary;

    }

}

// namespace pipesim
