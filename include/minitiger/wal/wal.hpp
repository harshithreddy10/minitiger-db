#ifndef MINITIGER_WAL_WAL_HPP
#define MINITIGER_WAL_WAL_HPP

#include "document.hpp"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace minitiger {

/**
 * @brief Operation types recorded in the Write-Ahead Log (WAL).
 * Inspired by WiredTiger journaling and the MongoDB Replica Set Oplog.
 */
enum class WalOpType : uint8_t {
    INSERT     = 0x01,
    UPDATE     = 0x02,
    DELETE_DOC = 0x03,
    CHECKPOINT = 0x04
};

/**
 * @brief Represents a single durable mutation event in the Write-Ahead Log.
 *
 * Binary Frame Format on Disk (Little-Endian):
 *   [4 bytes] Magic Frame Sync ("MWAL" = 0x4C41574D)
 *   [4 bytes] CRC32 Checksum of body bytes
 *   [4 bytes] Body Byte Length (uint32_t)
 *   --- Body starts here ---
 *   [8 bytes] Log Sequence Number (LSN, uint64_t)
 *   [1 byte ] WalOpType tag
 *   [4 bytes] Document ID length + UTF-8 bytes
 *   [4 bytes] Serialized Document payload length + Document bytes (0 for DELETE/CHECKPOINT)
 */
struct WalRecord {
    uint64_t                lsn{0};
    WalOpType               op_type{WalOpType::INSERT};
    std::string             doc_id;
    std::optional<Document> document;
};

class WriteAheadLog {
public:
    static constexpr uint32_t kFrameMagic = 0x4C41574D; // "MWAL"

    explicit WriteAheadLog(std::string wal_path, bool sync_on_write = true);
    ~WriteAheadLog();

    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;

    // Thread-safe WAL append operations returning the assigned Log Sequence Number (LSN)
    uint64_t append_insert(const Document& doc);
    uint64_t append_update(const Document& doc);
    uint64_t append_delete(const std::string& doc_id);
    uint64_t append_checkpoint();

    // Flush OS buffers to disk
    void flush();

    // Current latest LSN
    uint64_t current_lsn() const noexcept;

    /**
     * @brief Reads the WAL file from disk, verifies CRC32 checksums, safely skips
     * any half-written/corrupted tail frames from a power loss ("torn write"),
     * and returns all valid records after the last CHECKPOINT.
     */
    static std::vector<WalRecord> recover(const std::string& wal_path,
                                          size_t* torn_frames_skipped = nullptr);

    /**
     * @brief Replays recovered WAL records into an in-memory collection map.
     */
    static std::unordered_map<std::string, Document> replay_to_map(
        const std::vector<WalRecord>& records);

    /**
     * @brief Standard IEEE 802.3 CRC32 checksum calculation for frame integrity.
     */
    static uint32_t compute_crc32(const uint8_t* data, size_t length) noexcept;

private:
    uint64_t append_record_locked(WalOpType op_type,
                                  const std::string& doc_id,
                                  const std::optional<Document>& doc);

    std::string        wal_path_;
    bool               sync_on_write_;
    std::ofstream      out_stream_;
    uint64_t           next_lsn_{1};
    mutable std::mutex mutex_;
};

} // namespace minitiger

#endif // MINITIGER_WAL_WAL_HPP