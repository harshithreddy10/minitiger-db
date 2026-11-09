#include "lru_cache.hpp"
#include "storage_engine.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

using namespace minitiger;

void test_lru_cache_eviction() {
    std::cout << "[TEST 1] Running O(1) LRU Cache Recency & Eviction Test...\n";
    LRUCache cache(3);

    Document d1("doc_1"), d2("doc_2"), d3("doc_3"), d4("doc_4");
    d1.set("v", 1);
    d2.set("v", 2);
    d3.set("v", 3);
    d4.set("v", 4);

    assert(!cache.put("doc_1", d1).has_value());
    assert(!cache.put("doc_2", d2).has_value());
    assert(!cache.put("doc_3", d3).has_value());

    // Access doc_1 so it becomes Most Recently Used (MRU); doc_2 is now Least Recently Used (LRU)
    assert(cache.get("doc_1").has_value());

    // Inserting doc_4 into capacity-3 cache must evict doc_2
    auto evicted = cache.put("doc_4", d4);
    assert(evicted.has_value());
    assert(evicted->id() == "doc_2");

    // Verify doc_2 is a miss while doc_1, doc_3, doc_4 are hits
    assert(!cache.get("doc_2").has_value());
    assert(cache.get("doc_1").has_value());
    assert(cache.get("doc_3").has_value());
    assert(cache.get("doc_4").has_value());
    assert(cache.evictions() == 1);

    std::cout << "  -> [PASS] O(1) splice promotion and LRU eviction verified!\n\n";
}

void test_storage_engine_persistence_and_recovery() {
    std::cout << "[TEST 2] Running StorageEngine WAL Persistence & Restart Recovery Test...\n";
    const std::string wal_file = "test_part3_engine.wal";
    std::remove(wal_file.c_str());

    // 1. Open StorageEngine, insert 200 documents (with small cache capacity 64 to force evictions)
    {
        StorageEngine engine(wal_file, 64, false);
        for (int i = 0; i < 200; ++i) {
            Document doc("key_" + std::to_string(i));
            doc.set("index", i).set("service", "billing-worker");
            engine.put(doc);
        }
        engine.remove("key_50");

        // Even evicted documents should be transparently loaded from backing pages on get()
        auto fetched = engine.get("key_0");
        assert(fetched.has_value());
        assert(fetched->get_int64("index").value() == 0);
        assert(!engine.get("key_50").has_value());
    }

    // 2. Re-open StorageEngine from the same WAL file and verify all 199 live documents recovered
    {
        StorageEngine recovered_engine(wal_file, 128, false);
        auto st = recovered_engine.stats();
        assert(st.total_documents == 199);
        assert(recovered_engine.get("key_199").has_value());
        assert(!recovered_engine.get("key_50").has_value());
    }

    std::remove(wal_file.c_str());
    std::cout << "  -> [PASS] Transparent page reloads and full WAL restart recovery verified!\n\n";
}

void run_multithreaded_concurrency_benchmark() {
    std::cout << "[BENCHMARK] Running 8-Thread Striped Concurrency Stress Test (80% Read / 20% Write)...\n";
    const std::string wal_file = "test_part3_mt_bench.wal";
    std::remove(wal_file.c_str());

    constexpr size_t kNumThreads = 8;
    constexpr size_t kOpsPerThread = 25000;
    constexpr size_t kKeySpace = 2000;

    StorageEngine engine(wal_file, 1500, false);

    // Pre-populate 2,000 documents
    std::vector<float> sample_vec(64, 0.25f);
    for (size_t i = 0; i < kKeySpace; ++i) {
        Document doc("doc_" + std::to_string(i));
        doc.set("counter", static_cast<int64_t>(i))
           .set("embedding", sample_vec);
        engine.put(doc);
    }

    std::atomic<uint64_t> successful_reads{0};
    std::atomic<uint64_t> successful_writes{0};

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> workers;
    workers.reserve(kNumThreads);

    for (size_t tid = 0; tid < kNumThreads; ++tid) {
        workers.emplace_back([&, tid]() {
            for (size_t op = 0; op < kOpsPerThread; ++op) {
                size_t key_idx = (tid * 997 + op * 31) % kKeySpace;
                std::string doc_id = "doc_" + std::to_string(key_idx);

                if (op % 5 == 0) {
                    // 20% Write operations
                    Document updated(doc_id);
                    updated.set("counter", static_cast<int64_t>(op))
                           .set("writer_tid", static_cast<int64_t>(tid))
                           .set("embedding", sample_vec);
                    engine.put(updated);
                    successful_writes.fetch_add(1, std::memory_order_relaxed);
                } else {
                    // 80% Read operations
                    auto res = engine.get(doc_id);
                    if (res.has_value()) {
                        successful_reads.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });
    }

    for (auto& t : workers) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed_sec = std::chrono::duration<double>(end - start).count();
    size_t total_ops = kNumThreads * kOpsPerThread;
    double throughput = static_cast<double>(total_ops) / elapsed_sec;

    auto st = engine.stats();
    std::cout << "  -> Completed " << total_ops << " concurrent operations across " << kNumThreads
              << " threads in " << elapsed_sec << " sec\n";
    std::cout << "  -> Throughput: " << static_cast<uint64_t>(throughput) << " ops/sec (Reads: "
              << successful_reads.load() << ", Writes: " << successful_writes.load() << ")\n";
    std::cout << "  -> LRU Cache Hit Ratio: " << std::fixed << std::setprecision(2)
              << (st.cache_hit_ratio * 100.0) << "% (Hits: " << st.cache_hits
              << ", Misses: " << st.cache_misses << ", Evictions: " << st.cache_evictions << ")\n";

    std::remove(wal_file.c_str());
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  MiniTiger-DB Part 3: LRU Cache & 64-Stripe Concurrency Suite\n";
    std::cout << "=================================================================\n\n";

    test_lru_cache_eviction();
    test_storage_engine_persistence_and_recovery();
    run_multithreaded_concurrency_benchmark();

    std::cout << "\nAll Part 3 checks passed!\n";
    return 0;
}