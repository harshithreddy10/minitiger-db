#include "wal.hpp"

#include <cstring>
#include <stdexcept>

namespace minitiger {

namespace {

void write_u8(std::vector<uint8_t>& buf, uint8_t val) {
    buf.push_back(val);
}

void write_u32(std::vector<uint8_t>& buf, uint32_t val) {
    uint8_t bytes[4];
    std::memcpy(bytes, &val, sizeof(uint32_t));
    buf.insert(buf.end(), bytes, bytes + 4);
}

void write_u64(std::vector<uint8_t>& buf, uint64_t val) {
    uint8_t bytes[8];
    std::memcpy(bytes, &val, sizeof(uint64_t));
    buf.insert(buf.end(), bytes, bytes + 8);
}

void write_string(std::vector<uint8_t>& buf, const std::string& str) {
    write_u32(buf, static_cast<uint32_t>(str.size()));
    buf.insert(buf.end(), str.begin(), str.end());
}

} // namespace

WriteAheadLog::WriteAheadLog(std::string wal_path, bool sync_on_write)
    : wal_path_(std::move(wal_path)), sync_on_write_(sync_on_write) {
    // If an existing WAL exists on disk, scan it to set next_lsn_ past the highest LSN
    std::ifstream check_in(wal_path_, std::ios::binary);
    if (check_in.good()) {
        check_in.close();
        auto existing = recover(wal_path_);
        for (const auto& rec : existing) {
            if (rec.lsn >= next_lsn_) {
                next_lsn_ = rec.lsn + 1;
            }
        }
    }

    out_stream_.open(wal_path_, std::ios::binary | std::ios::app);
    if (!out_stream_.is_open()) {
        throw std::runtime_error("Failed to open Write-Ahead Log file: " + wal_path_);
    }
}

WriteAheadLog::~WriteAheadLog() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (out_stream_.is_open()) {
        out_stream_.flush();
        out_stream_.close();
    }
}

uint32_t WriteAheadLog::compute_crc32(const uint8_t* data, size_t length) noexcept {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

uint64_t WriteAheadLog::append_insert(const Document& doc) {
    std::lock_guard<std::mutex> lock(mutex_);
    return append_record_locked(WalOpType::INSERT, doc.id(), doc);
}

uint64_t WriteAheadLog::append_update(const Document& doc) {
    std::lock_guard<std::mutex> lock(mutex_);
    return append_record_locked(WalOpType::UPDATE, doc.id(), doc);
}

uint64_t WriteAheadLog::append_delete(const std::string& doc_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return append_record_locked(WalOpType::DELETE_DOC, doc_id, std::nullopt);
}

uint64_t WriteAheadLog::append_checkpoint() {
    std::lock_guard<std::mutex> lock(mutex_);
    return append_record_locked(WalOpType::CHECKPOINT, "", std::nullopt);
}

void WriteAheadLog::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    out_stream_.flush();
}

uint64_t WriteAheadLog::current_lsn() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_lsn_ - 1;
}

uint64_t WriteAheadLog::append_record_locked(WalOpType op_type,
                                            const std::string& doc_id,
                                            const std::optional<Document>& doc) {
    uint64_t lsn = next_lsn_++;

    // 1. Serialize the frame body: [8B LSN][1B OpType][String doc_id][4B DocLen + DocBytes]
    std::vector<uint8_t> body;
    body.reserve(128);
    write_u64(body, lsn);
    write_u8(body, static_cast<uint8_t>(op_type));
    write_string(body, doc_id);

    if (doc.has_value()) {
        std::vector<uint8_t> doc_bytes = doc->serialize();
        write_u32(body, static_cast<uint32_t>(doc_bytes.size()));
        body.insert(body.end(), doc_bytes.begin(), doc_bytes.end());
    } else {
        write_u32(body, 0);
    }

    // 2. Compute CRC32 checksum over the entire body
    uint32_t crc = compute_crc32(body.data(), body.size());
    uint32_t body_len = static_cast<uint32_t>(body.size());

    // 3. Assemble final frame: [4B Magic][4B CRC32][4B BodyLen][Body]
    std::vector<uint8_t> frame;
    frame.reserve(12 + body.size());
    write_u32(frame, kFrameMagic);
    write_u32(frame, crc);
    write_u32(frame, body_len);
    frame.insert(frame.end(), body.begin(), body.end());

    out_stream_.write(reinterpret_cast<const char*>(frame.data()),
                      static_cast<std::streamsize>(frame.size()));
    if (sync_on_write_) {
        out_stream_.flush();
    }

    return lsn;
}

std::vector<WalRecord> WriteAheadLog::recover(const std::string& wal_path,
                                              size_t* torn_frames_skipped) {
    if (torn_frames_skipped) {
        *torn_frames_skipped = 0;
    }

    std::ifstream in(wal_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return {};
    }

    std::streamsize file_size = in.tellg();
    if (file_size <= 0) {
        return {};
    }

    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(file_size));
    if (!in.read(reinterpret_cast<char*>(buf.data()), file_size)) {
        return {};
    }

    std::vector<WalRecord> records;
    size_t pos = 0;
    size_t total_len = buf.size();

    while (pos < total_len) {
        // Each frame header requires 12 bytes: [4B Magic][4B CRC32][4B BodyLen]
        if (pos + 12 > total_len) {
            if (torn_frames_skipped) (*torn_frames_skipped)++;
            break;
        }

        uint32_t magic = 0;
        uint32_t expected_crc = 0;
        uint32_t body_len = 0;
        std::memcpy(&magic, buf.data() + pos, 4);
        std::memcpy(&expected_crc, buf.data() + pos + 4, 4);
        std::memcpy(&body_len, buf.data() + pos + 8, 4);

        if (magic != kFrameMagic || pos + 12 + body_len > total_len) {
            // Torn write or corrupted header at tail of log
            if (torn_frames_skipped) (*torn_frames_skipped)++;
            break;
        }

        const uint8_t* body_ptr = buf.data() + pos + 12;
        uint32_t actual_crc = compute_crc32(body_ptr, body_len);
        if (actual_crc != expected_crc) {
            // Bit-rot or incomplete flush detected by CRC32 mismatch
            if (torn_frames_skipped) (*torn_frames_skipped)++;
            break;
        }

        // Parse body: [8B LSN][1B OpType][4B DocIdLen + DocId][4B DocPayloadLen + DocBytes]
        if (body_len < 17) {
            if (torn_frames_skipped) (*torn_frames_skipped)++;
            break;
        }

        size_t bpos = 0;
        uint64_t lsn = 0;
        std::memcpy(&lsn, body_ptr + bpos, 8);
        bpos += 8;

        auto op_type = static_cast<WalOpType>(body_ptr[bpos++]);

        uint32_t id_len = 0;
        std::memcpy(&id_len, body_ptr + bpos, 4);
        bpos += 4;
        if (bpos + id_len + 4 > body_len) {
            if (torn_frames_skipped) (*torn_frames_skipped)++;
            break;
        }

        std::string doc_id(reinterpret_cast<const char*>(body_ptr + bpos), id_len);
        bpos += id_len;

        uint32_t doc_len = 0;
        std::memcpy(&doc_len, body_ptr + bpos, 4);
        bpos += 4;
        if (bpos + doc_len != body_len) {
            if (torn_frames_skipped) (*torn_frames_skipped)++;
            break;
        }

        WalRecord rec;
        rec.lsn = lsn;
        rec.op_type = op_type;
        rec.doc_id = std::move(doc_id);

        if (doc_len > 0) {
            try {
                rec.document = Document::deserialize(body_ptr + bpos, doc_len);
            } catch (const std::exception&) {
                if (torn_frames_skipped) (*torn_frames_skipped)++;
                break;
            }
        }

        if (op_type == WalOpType::CHECKPOINT) {
            // A checkpoint means all prior WAL records are already flushed to data pages
            records.clear();
        } else {
            records.push_back(std::move(rec));
        }

        pos += 12 + body_len;
    }

    return records;
}

std::unordered_map<std::string, Document> WriteAheadLog::replay_to_map(
    const std::vector<WalRecord>& records) {
    std::unordered_map<std::string, Document> state;
    for (const auto& rec : records) {
        switch (rec.op_type) {
            case WalOpType::INSERT:
            case WalOpType::UPDATE:
                if (rec.document.has_value()) {
                    state[rec.doc_id] = *rec.document;
                }
                break;
            case WalOpType::DELETE_DOC:
                state.erase(rec.doc_id);
                break;
            case WalOpType::CHECKPOINT:
                break;
        }
    }
    return state;
}

} // namespace minitiger