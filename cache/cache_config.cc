#include "cache/cache_config.h"

#include "cache/cache_types.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pipesim {

    namespace cache {

        namespace {

            class JsonValue {

                public:   enum class Type {

                    kObject, kString, kInteger
                };

                static JsonValue Object(std::map<std::string, JsonValue> value) {

                    JsonValue result(Type::kObject);

                    result.object_ = std::move(value);

                    return result;

                }

                static JsonValue String(std::string value) {

                    JsonValue result(Type::kString);

                    result.string_ = std::move(value);

                    return result;

                }

                static JsonValue Integer(std::uint64_t value) {

                    JsonValue result(Type::kInteger);

                    result.integer_ = value;

                    return result;

                }

                Type type() const {

                    return type_;

                }

                const std::map<std::string, JsonValue>& object() const {

                    return object_;

                }

                const std::string& string() const {

                    return string_;

                }

                std::uint64_t integer() const {

                    return integer_;

                }

                private:   explicit JsonValue(Type type) : type_(type) {

                }

                Type type_;

                std::map<std::string, JsonValue> object_;

                std::string string_;

                std::uint64_t integer_ = 0;

            };

            class JsonParser {

                public:   explicit JsonParser(std::string text) : text_(std::move(text)) {

                }

                JsonValue Parse() {

                    SkipSpace();

                    JsonValue value = ParseValue();

                    SkipSpace();

                    if (position_ != text_.size()) Error("unexpected trailing input");

                    return value;

                }

                private:   [[noreturn]] void Error(const std::string& message) const {

                    std::ostringstream output;

                    output << "JSON byte " << position_ << ": " << message;

                    throw std::runtime_error(output.str());

                }

                void SkipSpace() {

                    while (position_ < text_.size() &&            std::isspace(static_cast<unsigned char>(text_[position_]))) {

                        ++position_;

                    }

                }

                bool Consume(char expected) {

                    SkipSpace();

                    if (position_ < text_.size() && text_[position_] == expected) {

                        ++position_;

                        return true;

                    }

                    return false;

                }

                JsonValue ParseValue() {

                    SkipSpace();

                    if (position_ >= text_.size()) Error("expected a value");

                    if (text_[position_] == '{') return ParseObject();

                    if (text_[position_] == '"') return JsonValue::String(ParseString());

                    if (std::isdigit(static_cast<unsigned char>(text_[position_]))) {

                        return JsonValue::Integer(ParseInteger());

                    }

                    Error("only objects, strings, and non-negative integers are supported");

                }

                JsonValue ParseObject() {

                    if (!Consume('{')) Error("expected '{'");

                    std::map<std::string, JsonValue> fields;

                    SkipSpace();

                    if (Consume('}')) return JsonValue::Object(std::move(fields));

                    while (true) {

                        SkipSpace();

                        if (position_ >= text_.size() || text_[position_] != '"') {

                            Error("expected an object key");

                        }

                        std::string key = ParseString();

                        if (!Consume(':')) Error("expected ':' after object key");

                        JsonValue value = ParseValue();

                        if (!fields.emplace(key, std::move(value)).second) {

                            Error("duplicate key '" + key + "'");

                        }

                        if (Consume('}')) break;

                        if (!Consume(',')) Error("expected ',' or '}'");

                    }

                    return JsonValue::Object(std::move(fields));

                }

                std::string ParseString() {

                    if (position_ >= text_.size() || text_[position_] != '"') {

                        Error("expected string");

                    }

                    ++position_;

                    std::string result;

                    while (position_ < text_.size()) {

                        const char value = text_[position_++];

                        if (value == '"') return result;

                        if (static_cast<unsigned char>(value) < 0x20) {

                            Error("control character in string");

                        }

                        if (value != '\\') {

                            result.push_back(value);

                            continue;

                        }

                        if (position_ >= text_.size()) Error("unterminated escape sequence");

                        const char escaped = text_[position_++];

                        switch (escaped) {

                            case '"': result.push_back('"');

                            break;

                            case '\\': result.push_back('\\');

                            break;

                            case '/': result.push_back('/');

                            break;

                            case 'b': result.push_back('\b');

                            break;

                            case 'f': result.push_back('\f');

                            break;

                            case 'n': result.push_back('\n');

                            break;

                            case 'r': result.push_back('\r');

                            break;

                            case 't': result.push_back('\t');

                            break;

                            default: Error("unsupported string escape");

                        }

                    }

                    Error("unterminated string");

                }

                std::uint64_t ParseInteger() {

                    if (text_[position_] == '0') {

                        ++position_;

                        if (position_ < text_.size() &&           std::isdigit(static_cast<unsigned char>(text_[position_]))) {

                            Error("integer has a leading zero");

                        }

                        return 0;

                    }

                    std::uint64_t result = 0;

                    while (position_ < text_.size() &&            std::isdigit(static_cast<unsigned char>(text_[position_]))) {

                        const unsigned digit = static_cast<unsigned>(text_[position_] - '0');

                        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {

                            Error("integer overflow");

                        }

                        result = result * 10U + digit;

                        ++position_;

                    }

                    return result;

                }

                std::string text_;

                std::size_t position_ = 0;

            };

            const JsonValue* RequireField(const JsonValue& object, const std::string& key,                               const std::string& context) {

                if (object.type() != JsonValue::Type::kObject) {

                    throw std::runtime_error(context + " must be a JSON object");

                }

                auto it = object.object().find(key);

                if (it == object.object().end()) {

                    throw std::runtime_error(context + " is missing key '" + key + "'");

                }

                return &it->second;

            }

            void RequireExactKeys(const JsonValue& object,                       const std::set<std::string>& expected,                       const std::string& context) {

                if (object.type() != JsonValue::Type::kObject) {

                    throw std::runtime_error(context + " must be a JSON object");

                }

                for (const auto& field : object.object()) {

                    if (expected.count(field.first) == 0) {

                        throw std::runtime_error(context + " contains unknown key '" +                                field.first + "'");

                    }

                }

                for (const std::string& key : expected) {

                    if (object.object().count(key) == 0) {

                        throw std::runtime_error(context + " is missing key '" + key + "'");

                    }

                }

            }

            std::uint32_t ReadU32(const JsonValue& object, const std::string& key,                       const std::string& context) {

                const JsonValue& value = *RequireField(object, key, context);

                if (value.type() != JsonValue::Type::kInteger ||       value.integer() > std::numeric_limits<std::uint32_t>::max()) {

                    throw std::runtime_error(context + "." + key +                              " must be a uint32 integer");

                }

                return static_cast<std::uint32_t>(value.integer());

            }

            std::string ReadString(const JsonValue& object, const std::string& key,                        const std::string& context) {

                const JsonValue& value = *RequireField(object, key, context);

                if (value.type() != JsonValue::Type::kString) {

                    throw std::runtime_error(context + "." + key + " must be a string");

                }

                return value.string();

            }

            CacheConfig ReadCache(const JsonValue& root, const std::string& key) {

                const JsonValue& object = *RequireField(root, key, "root");

                RequireExactKeys(object,                    {

                    "line_size", "sets", "ways", "mshr_entries",                     "mshr_waiters_per_entry", "replacement",                     "writeback_entries", "writeback_waiters_per_entry",                     "fill_mode"
                },                    key);

                CacheConfig config;

                config.line_size = ReadU32(object, "line_size", key);

                config.sets = ReadU32(object, "sets", key);

                config.ways = ReadU32(object, "ways", key);

                config.mshr_entries = ReadU32(object, "mshr_entries", key);

                config.mshr_waiters_per_entry =       ReadU32(object, "mshr_waiters_per_entry", key);

                config.writeback_entries = ReadU32(object, "writeback_entries", key);

                config.writeback_waiters_per_entry =       ReadU32(object, "writeback_waiters_per_entry", key);

                const std::string replacement = ReadString(object, "replacement", key);

                if (replacement == "tree_plru") {

                    config.replacement = ReplacementKind::kTreePlru;

                }

                else if (replacement == "srrip") {

                    config.replacement = ReplacementKind::kSrrip;

                }

                else {

                    throw std::runtime_error(key + ".replacement must be 'tree_plru' or 'srrip'");

                }

                if (ReadString(object, "fill_mode", key) != "complete_line") {

                    throw std::runtime_error(key + ".fill_mode must be 'complete_line'");

                }

                return config;

            }

            MemoryConfig ReadMemory(const JsonValue& root, const std::string& key) {

                const JsonValue& object = *RequireField(root, key, "root");

                RequireExactKeys(object, {

                    "latency", "max_outstanding", "image_bytes"
                }, key);

                MemoryConfig config;

                config.latency = ReadU32(object, "latency", key);

                config.max_outstanding = ReadU32(object, "max_outstanding", key);

                config.image_bytes = ReadU32(object, "image_bytes", key);

                return config;

            }

            ::pipesim::ExecutionUnitConfig ReadExecutionUnit(     const JsonValue& units, const std::string& key) {

                const JsonValue& object = *RequireField(units, key, "ExecutionUnits");

                RequireExactKeys(object, {

                    "latency", "initiation_interval"
                },                    "ExecutionUnits." + key);

                ::pipesim::ExecutionUnitConfig config;

                config.latency = ReadU32(object, "latency", "ExecutionUnits." + key);

                config.initiation_interval =       ReadU32(object, "initiation_interval", "ExecutionUnits." + key);

                if (config.initiation_interval == 0) {

                    throw std::runtime_error("ExecutionUnits." + key +                              ".initiation_interval must be positive");

                }

                if (static_cast<std::uint64_t>(config.latency) + 1U >       std::vector<int>().max_size()) {

                    throw std::runtime_error("ExecutionUnits." + key +                              " stage allocation is too large");

                }

                return config;

            }

            void CheckedProduct(std::uint64_t first, std::uint64_t second,                     const std::string& description) {

                if (first != 0 && second > std::numeric_limits<std::size_t>::max() / first) {

                    throw std::runtime_error("configured allocation overflows: " + description);

                }

            }

            void ValidateCache(const CacheConfig& config, const std::string& name,                    bool instruction_side) {

                if (config.line_size < 4 || !IsPowerOfTwo(config.line_size)) {

                    throw std::runtime_error(name + ".line_size must be a power of two >= 4");

                }

                if (!IsPowerOfTwo(config.sets)) {

                    throw std::runtime_error(name + ".sets must be a positive power of two");

                }

                if (config.ways == 0 || config.mshr_entries == 0 ||       config.mshr_waiters_per_entry == 0) {

                    throw std::runtime_error(name + " ways/MSHR capacities must be positive");

                }

                if (config.replacement == ReplacementKind::kTreePlru &&       !IsPowerOfTwo(config.ways)) {

                    throw std::runtime_error(name + " Tree-PLRU requires power-of-two ways");

                }

                if (instruction_side) {

                    if (config.writeback_entries != 0 ||         config.writeback_waiters_per_entry != 0) {

                        throw std::runtime_error(name + " instruction cache cannot have writebacks");

                    }

                }

                else if (config.writeback_entries == 0 ||              config.writeback_waiters_per_entry == 0) {

                    throw std::runtime_error(name + " data-cache writeback capacities must be positive");

                }

                CheckedProduct(config.sets, config.ways, name + " line count");

                CheckedProduct(static_cast<std::uint64_t>(config.sets) * config.ways,                  config.line_size, name + " data array");

                CheckedProduct(config.mshr_entries, config.mshr_waiters_per_entry,                  name + " MSHR waiter storage");

                CheckedProduct(config.writeback_entries, config.writeback_waiters_per_entry,                  name + " writeback waiter storage");

            }

            void ValidateMemory(const MemoryConfig& config, const std::string& name) {

                if (config.latency == 0 || config.max_outstanding == 0) {

                    throw std::runtime_error(name + " latency/max_outstanding must be positive");

                }

                if (config.image_bytes < 4 || config.image_bytes % 4 != 0) {

                    throw std::runtime_error(name + ".image_bytes must be a positive multiple of 4");

                }

                CheckedProduct(config.max_outstanding, config.image_bytes,                  name + " bounded outstanding metadata");

            }

        }

        // namespace

        bool LoadSystemConfig(const std::string& file_name, SystemConfig* config,                       std::string* error) {

            if (config == nullptr || error == nullptr) return false;

            try {

                std::ifstream input(file_name);

                if (!input) throw std::runtime_error("cannot open configuration file");

                std::ostringstream contents;

                contents << input.rdbuf();

                JsonValue root = JsonParser(contents.str()).Parse();

                RequireExactKeys(root,                      {

                    "L1I", "L2I", "L1D", "L2D", "CPU",                       "ExecutionUnits", "InstructionMemory", "DataMemory"
                },                      "root");

                SystemConfig parsed;

                parsed.l1i = ReadCache(root, "L1I");

                parsed.l2i = ReadCache(root, "L2I");

                parsed.l1d = ReadCache(root, "L1D");

                parsed.l2d = ReadCache(root, "L2D");

                const JsonValue& cpu = *RequireField(root, "CPU", "root");

                RequireExactKeys(cpu,                      {

                    "fetch_queue_entries", "pending_data_entries",                       "completion_queue_entries"
                },                      "CPU");

                parsed.cpu.fetch_queue_entries =         ReadU32(cpu, "fetch_queue_entries", "CPU");

                parsed.cpu.pending_data_entries =         ReadU32(cpu, "pending_data_entries", "CPU");

                parsed.cpu.completion_queue_entries =         ReadU32(cpu, "completion_queue_entries", "CPU");

                if (parsed.cpu.fetch_queue_entries == 0 ||         parsed.cpu.pending_data_entries == 0 ||         parsed.cpu.completion_queue_entries == 0) {

                    throw std::runtime_error("all CPU queue capacities must be positive");

                }

                const JsonValue& execution_units =         *RequireField(root, "ExecutionUnits", "root");

                const std::set<std::string> execution_names = {

                    "ADD", "SUB", "AND", "OR", "XOR", "CONTROL", "AGU"
                };

                RequireExactKeys(execution_units, execution_names, "ExecutionUnits");

                const char* ordered_names[] = {

                    "ADD", "SUB", "AND", "OR", "XOR", "CONTROL", "AGU"
                };

                for (std::size_t index = 0; index < parsed.execution_units.size(); ++index) {

                    parsed.execution_units[index] =           ReadExecutionUnit(execution_units, ordered_names[index]);

                }

                parsed.instruction_memory = ReadMemory(root, "InstructionMemory");

                parsed.data_memory = ReadMemory(root, "DataMemory");

                ValidateCache(parsed.l1i, "L1I", true);

                ValidateCache(parsed.l2i, "L2I", true);

                ValidateCache(parsed.l1d, "L1D", false);

                ValidateCache(parsed.l2d, "L2D", false);

                ValidateMemory(parsed.instruction_memory, "InstructionMemory");

                ValidateMemory(parsed.data_memory, "DataMemory");

                if (parsed.l1i.line_size > parsed.l2i.line_size) {

                    throw std::runtime_error("L1I.line_size must not exceed L2I.line_size");

                }

                if (parsed.l1d.line_size > parsed.l2d.line_size) {

                    throw std::runtime_error("L1D.line_size must not exceed L2D.line_size");

                }

                *config = parsed;

                return true;

            }

            catch (const std::exception& exception) {

                *error = exception.what();

                return false;

            }

        }

        const char* ReplacementKindName(ReplacementKind replacement) {

            switch (replacement) {

                case ReplacementKind::kTreePlru: return "tree_plru";

                case ReplacementKind::kSrrip: return "srrip";

            }

            return "unknown";

        }

    }

    // namespace cache
}

// namespace pipesim
