#ifndef MINITIGER_DOC_DOCUMENT_HPP
#define MINITIGER_DOC_DOCUMENT_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace minitiger {

/**
 * @brief Wire-format type tags inspired by MongoDB's BSON specification.
 * Each field written to disk or network is prefixed with a 1-byte FieldType tag.
 */
enum class FieldType : uint8_t {
    INT64      = 0x01,
    DOUBLE     = 0x02,
    STRING     = 0x03,
    BOOL       = 0x04,
    VECTOR_F32 = 0x05  // Contiguous 32-bit float array for AI/Vector Search embeddings
};

/**
 * @brief Strongly-typed C++17 value representation for a document field.
 */
using Value = std::variant<int64_t, double, std::string, bool, std::vector<float>>;

/**
 * @brief Represents a binary-serializable document in MiniTiger-DB.
 *
 * Layout of a serialized Document buffer (Little-Endian):
 *   [4 bytes] Magic Header ("MTGR" = 0x5247544D)
 *   [1 byte ] Wire Format Version (0x01)
 *   [4 bytes] Total Payload Byte Length (uint32_t)
 *   [4 bytes] Document ID (_id) length + UTF-8 bytes
 *   [4 bytes] Number of fields (uint32_t)
 *   For each field:
 *     [1 byte ] FieldType tag
 *     [4 bytes] Key string length + UTF-8 bytes
 *     [N bytes] Value payload (fixed width for primitives, length-prefixed for string/vector)
 */
class Document {
public:
    static constexpr uint32_t kMagicHeader = 0x5247544D; // "MTGR"
    static constexpr uint8_t  kWireVersion = 0x01;

    Document() = default;
    explicit Document(std::string id);

    // Document ID accessors
    const std::string& id() const noexcept { return id_; }
    void set_id(std::string id) { id_ = std::move(id); }

    // Fluent field mutators
    Document& set(const std::string& key, int64_t val);
    Document& set(const std::string& key, int val) { return set(key, static_cast<int64_t>(val)); }
    Document& set(const std::string& key, double val);
    Document& set(const std::string& key, const std::string& val);
    Document& set(const std::string& key, const char* val) { return set(key, std::string(val)); }
    Document& set(const std::string& key, bool val);
    Document& set(const std::string& key, std::vector<float> val);

    // Field inspection & type-safe getters
    bool has_field(const std::string& key) const noexcept;
    size_t field_count() const noexcept { return fields_.size(); }
    const std::map<std::string, Value>& fields() const noexcept { return fields_; }

    std::optional<int64_t>            get_int64(const std::string& key) const noexcept;
    std::optional<double>             get_double(const std::string& key) const noexcept;
    std::optional<std::string>        get_string(const std::string& key) const noexcept;
    std::optional<bool>               get_bool(const std::string& key) const noexcept;
    std::optional<std::vector<float>> get_vector(const std::string& key) const noexcept;

    // Binary BSON-style serialization & zero-copy buffer deserialization
    std::vector<uint8_t> serialize() const;
    static Document deserialize(const uint8_t* data, size_t length);
    static Document deserialize(const std::vector<uint8_t>& buffer);

    // Human-readable diagnostic output (JSON-like representation)
    std::string to_json_string() const;

    bool operator==(const Document& other) const noexcept;
    bool operator!=(const Document& other) const noexcept { return !(*this == other); }

private:
    std::string id_;
    std::map<std::string, Value> fields_;
};

} // namespace minitiger

#endif // MINITIGER_DOC_DOCUMENT_HPP