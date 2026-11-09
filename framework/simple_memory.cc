#include "framework/simple_memory.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pipesim {

    namespace framework {

        namespace {

            std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment) {

                if (alignment == 0 || !cache::IsPowerOfTwo(alignment)) {

                    throw std::runtime_error("invalid memory-image alignment");

                }

                const std::uint64_t result =       (static_cast<std::uint64_t>(value) + alignment - 1U) &       ~(static_cast<std::uint64_t>(alignment) - 1U);

                if (result > std::numeric_limits<std::uint32_t>::max()) {

                    throw std::runtime_error("padded memory image exceeds uint32 address space");

                }

                return static_cast<std::uint32_t>(result);

            }

        }

        // namespace

        ByteImage::ByteImage(std::uint32_t logical_size,                      std::uint32_t padding_alignment) {

            Configure(logical_size, padding_alignment);

        }

        void ByteImage::Configure(std::uint32_t logical_size,                           std::uint32_t padding_alignment) {

            if (logical_size == 0) {

                throw std::runtime_error("memory image cannot be empty");

            }

            logical_size_ = logical_size;

            padded_size_ = AlignUp(logical_size, padding_alignment);

            initial_.assign(padded_size_, 0);

            current_ = initial_;

        }

        void ByteImage::SetInitialWord(std::uint32_t address, std::uint32_t value) {

            if (address % 4 != 0 ||       static_cast<std::uint64_t>(address) + 4 > logical_size_) {

                throw std::runtime_error("initial word lies outside logical memory image");

            }

            const std::vector<std::uint8_t> data = cache::WordToBytes(value);

            std::copy(data.begin(), data.end(), initial_.begin() + address);

        }

        void ByteImage::SealInitialImage() {

            current_ = initial_;

        }

        void ByteImage::Reset() {

            current_ = initial_;

        }

        void ByteImage::CheckRange(std::uint32_t address, std::uint32_t size) const {

            if (size == 0 || static_cast<std::uint64_t>(address) + size > padded_size_) {

                throw std::runtime_error("memory endpoint access exceeds padded image");

            }

        }

        std::vector<std::uint8_t> ByteImage::Read(std::uint32_t address,                                          std::uint32_t size) const {

            CheckRange(address, size);

            return std::vector<std::uint8_t>(current_.begin() + address,                                    current_.begin() + address + size);

        }

        void ByteImage::Write(std::uint32_t address,                       const std::vector<std::uint8_t>& data) {

            CheckRange(address, static_cast<std::uint32_t>(data.size()));

            std::copy(data.begin(), data.end(), current_.begin() + address);

        }

        SimpleMemory::SimpleMemory(std::string name, bool instruction_side,                            cache::MemoryConfig config, ByteImage* image)     : name_(std::move(name)),       instruction_side_(instruction_side),       config_(config),       image_(image) {

            if (image_ == nullptr) throw std::runtime_error("memory endpoint image is null");

        }

        void SimpleMemory::Reset() {

            current_.clear();

            next_.clear();

            pending_write_.reset();

        }

        void SimpleMemory::BeginCycle() {

            next_ = current_;

            pending_write_.reset();

        }

        MemoryIntent SimpleMemory::Inspect(std::uint64_t current_cycle) const {

            MemoryIntent intent;

            intent.can_accept_request = current_.size() < config_.max_outstanding;

            if (!current_.empty() && current_.front().ready_cycle <= current_cycle) {

                intent.response = current_.front().materialized                           ? current_.front().response                           : Materialize(current_.front());

            }

            return intent;

        }

        void SimpleMemory::ValidateRequest(const cache::CacheRequest& request) const {

            cache::ValidateRequestPayload(request);

            if (instruction_side_ && request.type != cache::RequestType::kRead) {

                throw std::runtime_error(name_ + " accepts only read requests");

            }

            if (!instruction_side_ && request.type != cache::RequestType::kRead &&       request.type != cache::RequestType::kWriteback) {

                throw std::runtime_error(name_ + " accepts only reads and writebacks");

            }

            if (static_cast<std::uint64_t>(request.address) + request.size >       image_->padded_size()) {

                throw std::runtime_error(name_ + " request exceeds padded image");

            }

        }

        cache::CacheResponse SimpleMemory::Materialize(const Outstanding& entry) const {

            cache::CacheResponse response;

            response.id = entry.request.id;

            response.origin_id = entry.request.origin_id;

            response.sequence = entry.request.sequence;

            response.generation = entry.request.generation;

            response.address = entry.request.address;

            response.size = entry.request.size;

            if (entry.request.type == cache::RequestType::kRead) {

                response.type = cache::ResponseType::kReadData;

                response.data = image_->Read(entry.request.address, entry.request.size);

            }

            else {

                response.type = cache::ResponseType::kWritebackAck;

            }

            cache::ValidateResponsePayload(response);

            return response;

        }

        void SimpleMemory::Operate(std::uint64_t current_cycle,                            const cache::CacheRequest* accepted_request,                            bool response_granted) {

            if (!current_.empty() && current_.front().ready_cycle <= current_cycle) {

                if (!next_.front().materialized) {

                    next_.front().response = Materialize(current_.front());

                    next_.front().materialized = true;

                    if (current_.front().request.type == cache::RequestType::kWriteback) {

                        pending_write_ = std::make_pair(current_.front().request.address,                                         current_.front().request.data);

                    }

                }

                if (response_granted) next_.pop_front();

            }

            else if (response_granted) {

                throw std::runtime_error(name_ + " response granted without an eligible response");

            }

            if (accepted_request != nullptr) {

                if (current_.size() >= config_.max_outstanding) {

                    throw std::runtime_error(name_ + " accepted request while full");

                }

                ValidateRequest(*accepted_request);

                const std::uint64_t delay =         static_cast<std::uint64_t>(config_.latency) + 1U;

                if (current_cycle > std::numeric_limits<std::uint64_t>::max() - delay) {

                    throw std::runtime_error(name_ + " ready-cycle overflow");

                }

                Outstanding entry;

                entry.request = *accepted_request;

                entry.accepted_cycle = current_cycle + 1U;

                entry.ready_cycle = current_cycle + delay;

                next_.push_back(std::move(entry));

            }

        }

        void SimpleMemory::Commit() {

            if (pending_write_) image_->Write(pending_write_->first, pending_write_->second);

            current_ = std::move(next_);

            next_ = current_;

            pending_write_.reset();

        }

        std::optional<std::uint64_t> SimpleMemory::NextReadyCycle() const {

            if (current_.empty()) return std::nullopt;

            return current_.front().ready_cycle;

        }

        std::vector<MemoryOutstandingSnapshot> SimpleMemory::Snapshot() const {

            std::vector<MemoryOutstandingSnapshot> result;

            result.reserve(current_.size());

            for (const Outstanding& entry : current_) {

                result.push_back( {

                    entry.request.id, entry.request.address,                       entry.request.size, entry.accepted_cycle,                       entry.ready_cycle, entry.materialized
                });

            }

            return result;

        }

    }

    // namespace framework
}

// namespace pipesim
