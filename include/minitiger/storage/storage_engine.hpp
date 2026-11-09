#ifndef MINITIGER_STORAGE_STORAGE_ENGINE_HPP
#define MINITIGER_STORAGE_STORAGE_ENGINE_HPP

#include "document.hpp"
#include "lru_cache.hpp"
#include "wal.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace minitiger {

/**
 * @brief Diagnostic statistics for the StorageEngine.
 */
struct StorageStats {
    size_t   total_documents{0};
    size_t   cached_documents{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
    uint64_t cache_evictions{0};
    double   cache_hit_ratio{0.0};
    uint64_t wal_lsn{0};
};

/**
 * @brief Multithreaded Document Storage Engine with 64-Way Lock Striping,
 * O(1) LRU Page Caching, and Write-Ahead Log Crash Recovery.
 *
 * Inspired by MongoDB's WiredTiger Storage Engine:
 * - Uses 64 independent stripes (each with its own std::shared_mutex, LRU cache
 *   partition, and serialized backing store) so concurrent reader and writer threads
 *   operate in parallel without contending on a global database lock.
 * - Persists all mutations to the WriteAheadLog before updating in-memory state.
 */
class StorageEngine {
public:
    static constexpr size_t kNumStripes = 64;

    explicit StorageEngine(const std::string& wal_path,
                           size_t total_cache_capacity = 4096,
                           bool sync_wal_on_write = false);

    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;

    // Thread-safe CRUD operations with fine-grained striped locking
    uint64_t put(const Document& doc);
    std::optional<Document> get(const std::string& doc_id);
    bool remove(const std::string& doc_id);

    // Flushes WAL and writes a checkpoint record
    uint64_t checkpoint();

    // Scans all documents across stripes (used by index builders and recovery checks)
    std::vector<Document> scan_all() const;

    // Aggregated storage and cache metrics across all 64 stripes
    StorageStats stats() const;

private:
    struct Stripe {
        mutable std::shared_mutex                          mutex;
        std::unique_ptr<LRUCache>                          cache;
        // Backing store holds binary-serialized document buffers (simulating disk pages)
        std::unordered_map<std::string, std::vector<uint8_t>> disk_pages;
    };

    size_t stripe_index(const std::string& doc_id) const noexcept;

    WriteAheadLog                     wal_;
    std::array<Stripe, kNumStripes>   stripes_;
};

} // namespace minitiger

#endif // MINITIGER_STORAGE_STORAGE_ENGINE_HPP