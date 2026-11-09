#include "document.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace minitiger {

namespace {

// Helper functions to append primitive values in little-endian byte order
void write_u8(std::vector<uint8_t>& buf, uint8_t val) {
    buf.push_back(val);
}

void write_u32(std::vector<uint8_t>& buf, uint32_t val) {
    uint8_t bytes[4];
    std::memcpy(bytes, &val, sizeof(uint32_t));
    buf.insert(buf.end(), bytes, bytes + 4);
}

void write_i64(std::vector<uint8_t>& buf, int64_t val) {
    uint8_t bytes[8];
    std::memcpy(bytes, &val, sizeof(int64_t));
    buf.insert(buf.end(), bytes, bytes + 8);
}

void write_f64(std::vector<uint8_t>& buf, double val) {
    uint8_t bytes[8];
    std::memcpy(bytes, &val, sizeof(double));
    buf.insert(buf.end(), bytes, bytes + 8);
}

void write_string(std::vector<uint8_t>& buf, const std::string& str) {
    write_u32(buf, static_cast<uint32_t>(str.size()));
    buf.insert(buf.end(), str.begin(), str.end());
}

// Bounds-checked cursor reader for binary deserialization
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t len) : data_(data), len_(len), pos_(0) {}

    uint8_t read_u8() {
        require(1);
        return data_[pos_++];
    }

    uint32_t read_u32() {
        require(sizeof(uint32_t));
        uint32_t val = 0;
        std::memcpy(&val, data_ + pos_, sizeof(uint32_t));
        pos_ += sizeof(uint32_t);
        return val;
    }

    int64_t read_i64() {
        require(sizeof(int64_t));
        int64_t val = 0;
        std::memcpy(&val, data_ + pos_, sizeof(int64_t));
        pos_ += sizeof(int64_t);
        return val;
    }

    double read_f64() {
        require(sizeof(double));
        double val = 0.0;
        std::memcpy(&val, data_ + pos_, sizeof(double));
        pos_ += sizeof(double);
        return val;
    }

    std::string read_string() {
        uint32_t size = read_u32();
        require(size);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), size);
        pos_ += size;
        return s;
    }

    std::vector<float> read_vector_f32() {
        uint32_t dim = read_u32();
        size_t byte_len = static_cast<size_t>(dim) * sizeof(float);
        require(byte_len);
        std::vector<float> vec(dim);
        if (dim > 0) {
            std::memcpy(vec.data(), data_ + pos_, byte_len);
        }
        pos_ += byte_len;
        return vec;
    }

    size_t remaining() const noexcept { return len_ - pos_; }

private:
    void require(size_t bytes) const {
        if (pos_ + bytes > len_) {
            throw std::runtime_error("Corrupted or truncated MiniTiger document buffer");
        }
    }

    const uint8_t* data_;
    size_t len_;
    size_t pos_;
};

} // namespace

Document::Document(std::string id) : id_(std::move(id)) {}

Document& Document::set(const std::string& key, int64_t val) {
    fields_[key] = val;
    return *this;
}

Document& Document::set(const std::string& key, double val) {
    fields_[key] = val;
    return *this;
}

Document& Document::set(const std::string& key, const std::string& val) {
    fields_[key] = val;
    return *this;
}

Document& Document::set(const std::string& key, bool val) {
    fields_[key] = val;
    return *this;
}

Document& Document::set(const std::string& key, std::vector<float> val) {
    fields_[key] = std::move(val);
    return *this;
}

bool Document::has_field(const std::string& key) const noexcept {
    return fields_.find(key) != fields_.end();
}

std::optional<int64_t> Document::get_int64(const std::string& key) const noexcept {
    auto it = fields_.find(key);
    if (it != fields_.end() && std::holds_alternative<int64_t>(it->second)) {
        return std::get<int64_t>(it->second);
    }
    return std::nullopt;
}

std::optional<double> Document::get_double(const std::string& key) const noexcept {
    auto it = fields_.find(key);
    if (it != fields_.end() && std::holds_alternative<double>(it->second)) {
        return std::get<double>(it->second);
    }
    return std::nullopt;
}

std::optional<std::string> Document::get_string(const std::string& key) const noexcept {
    auto it = fields_.find(key);
    if (it != fields_.end() && std::holds_alternative<std::string>(it->second)) {
        return std::get<std::string>(it->second);
    }
    return std::nullopt;
}

std::optional<bool> Document::get_bool(const std::string& key) const noexcept {
    auto it = fields_.find(key);
    if (it != fields_.end() && std::holds_alternative<bool>(it->second)) {
        return std::get<bool>(it->second);
    }
    return std::nullopt;
}

std::optional<std::vector<float>> Document::get_vector(const std::string& key) const noexcept {
    auto it = fields_.find(key);
    if (it != fields_.end() && std::holds_alternative<std::vector<float>>(it->second)) {
        return std::get<std::vector<float>>(it->second);
    }
    return std::nullopt;
}

std::vector<uint8_t> Document::serialize() const {
    std::vector<uint8_t> buf;
    buf.reserve(128);

    // 1. Write 4-byte Magic Header and 1-byte Wire Version
    write_u32(buf, kMagicHeader);
    write_u8(buf, kWireVersion);

    // 2. Reserve 4 bytes for total byte length (patched at the end)
    size_t length_offset = buf.size();
    write_u32(buf, 0);

    // 3. Write Document _id and field count
    write_string(buf, id_);
    write_u32(buf, static_cast<uint32_t>(fields_.size()));

    // 4. Encode each key-value pair with its 1-byte FieldType tag
    for (const auto& [key, val] : fields_) {
        if (std::holds_alternative<int64_t>(val)) {
            write_u8(buf, static_cast<uint8_t>(FieldType::INT64));
            write_string(buf, key);
            write_i64(buf, std::get<int64_t>(val));
        } else if (std::holds_alternative<double>(val)) {
            write_u8(buf, static_cast<uint8_t>(FieldType::DOUBLE));
            write_string(buf, key);
            write_f64(buf, std::get<double>(val));
        } else if (std::holds_alternative<std::string>(val)) {
            write_u8(buf, static_cast<uint8_t>(FieldType::STRING));
            write_string(buf, key);
            write_string(buf, std::get<std::string>(val));
        } else if (std::holds_alternative<bool>(val)) {
            write_u8(buf, static_cast<uint8_t>(FieldType::BOOL));
            write_string(buf, key);
            write_u8(buf, std::get<bool>(val) ? 1 : 0);
        } else if (std::holds_alternative<std::vector<float>>(val)) {
            write_u8(buf, static_cast<uint8_t>(FieldType::VECTOR_F32));
            write_string(buf, key);
            const auto& vec = std::get<std::vector<float>>(val);
            write_u32(buf, static_cast<uint32_t>(vec.size()));
            if (!vec.empty()) {
                const auto* raw_bytes = reinterpret_cast<const uint8_t*>(vec.data());
                buf.insert(buf.end(), raw_bytes, raw_bytes + vec.size() * sizeof(float));
            }
        }
    }

    // 5. Patch total serialized byte size into header
    uint32_t total_size = static_cast<uint32_t>(buf.size());
    std::memcpy(buf.data() + length_offset, &total_size, sizeof(uint32_t));
    return buf;
}

Document Document::deserialize(const uint8_t* data, size_t length) {
    ByteReader reader(data, length);

    uint32_t magic = reader.read_u32();
    if (magic != kMagicHeader) {
        throw std::runtime_error("Invalid Document magic header");
    }

    uint8_t version = reader.read_u8();
    if (version != kWireVersion) {
        throw std::runtime_error("Unsupported Document wire format version");
    }

    uint32_t total_size = reader.read_u32();
    if (total_size != length) {
        throw std::runtime_error("Document byte size mismatch");
    }

    Document doc(reader.read_string());
    uint32_t num_fields = reader.read_u32();

    for (uint32_t i = 0; i < num_fields; ++i) {
        auto type = static_cast<FieldType>(reader.read_u8());
        std::string key = reader.read_string();

        switch (type) {
            case FieldType::INT64:
                doc.set(key, reader.read_i64());
                break;
            case FieldType::DOUBLE:
                doc.set(key, reader.read_f64());
                break;
            case FieldType::STRING:
                doc.set(key, reader.read_string());
                break;
            case FieldType::BOOL:
                doc.set(key, reader.read_u8() != 0);
                break;
            case FieldType::VECTOR_F32:
                doc.set(key, reader.read_vector_f32());
                break;
            default:
                throw std::runtime_error("Unknown FieldType tag encountered during deserialization");
        }
    }

    return doc;
}

Document Document::deserialize(const std::vector<uint8_t>& buffer) {
    return deserialize(buffer.data(), buffer.size());
}

std::string Document::to_json_string() const {
    std::ostringstream oss;
    oss << "{ \"_id\": \"" << id_ << "\"";
    for (const auto& [key, val] : fields_) {
        oss << ", \"" << key << "\": ";
        if (std::holds_alternative<int64_t>(val)) {
            oss << std::get<int64_t>(val);
        } else if (std::holds_alternative<double>(val)) {
            oss << std::fixed << std::setprecision(4) << std::get<double>(val);
        } else if (std::holds_alternative<std::string>(val)) {
            oss << "\"" << std::get<std::string>(val) << "\"";
        } else if (std::holds_alternative<bool>(val)) {
            oss << (std::get<bool>(val) ? "true" : "false");
        } else if (std::holds_alternative<std::vector<float>>(val)) {
            const auto& vec = std::get<std::vector<float>>(val);
            oss << "[vector dim=" << vec.size();
            if (!vec.empty()) {
                oss << ", head=(" << std::fixed << std::setprecision(3) << vec[0];
                if (vec.size() > 1) oss << ", " << vec[1];
                oss << "...)]";
            } else {
                oss << "]";
            }
        }
    }
    oss << " }";
    return oss.str();
}

bool Document::operator==(const Document& other) const noexcept {
    return id_ == other.id_ && fields_ == other.fields_;
}

} // namespace minitiger