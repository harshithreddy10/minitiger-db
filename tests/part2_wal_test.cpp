#include "wal.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

using namespace minitiger;

void test_wal_replay_and_checkpoint() {
    std::cout << "[TEST 1] Running Write-Ahead Log (WAL) Append, Checkpoint & Replay Test...\n";
    const std::string wal_file = "test_part2_normal.wal";
    std::remove(wal_file.c_str());

    {
        WriteAheadLog wal(wal_file, true);

        Document d1("doc_101");
        d1.set("service", "auth-service").set("status", 200);
        assert(wal.append_insert(d1) == 1);

        // Emit a checkpoint (records before checkpoint shouldn't need replay)
        assert(wal.append_checkpoint() == 2);

        Document d2("doc_202");
        d2.set("service", "payment-service").set("latency_ms", 48.5);
        assert(wal.append_insert(d2) == 3);

        Document d2_updated("doc_202");
        d2_updated.set("service", "payment-service").set("latency_ms", 19.2);
        assert(wal.append_update(d2_updated) == 4);

        Document d3("doc_303");
        d3.set("service", "search-router").set("status", 503);
        assert(wal.append_insert(d3) == 5);
        assert(wal.append_delete("doc_303") == 6);
    }

    size_t torn_skipped = 0;
    auto records = WriteAheadLog::recover(wal_file, &torn_skipped);
    assert(torn_skipped == 0);
    // 4 operations occurred after the checkpoint at LSN 2 (LSNs 3, 4, 5, 6)
    assert(records.size() == 4);
    assert(records.front().lsn == 3);
    assert(records.back().lsn == 6);

    auto state = WriteAheadLog::replay_to_map(records);
    assert(state.size() == 1);
    assert(state.count("doc_202") == 1);
    assert(state.at("doc_202").get_double("latency_ms").value() == 19.2);
    assert(state.count("doc_303") == 0);

    std::remove(wal_file.c_str());
    std::cout << "  -> [PASS] WAL LSN sequencing, checkpointing, and state replay verified!\n\n";
}

void test_torn_write_crash_recovery() {
    std::cout << "[TEST 2] Simulating Power Loss (Torn Write & Bit-Rot CRC32 Detection)...\n";
    const std::string wal_file = "test_part2_crash.wal";
    std::remove(wal_file.c_str());

    {
        WriteAheadLog wal(wal_file, true);
        for (int i = 1; i <= 5; ++i) {
            Document doc("incident_" + std::to_string(i));
            doc.set("severity", i).set("embedding", std::vector<float>(64, 0.5f));
            wal.append_insert(doc);
        }
    }

    // Simulate a server power loss mid-write by appending a corrupted/truncated tail frame
    {
        std::ofstream corrupt_out(wal_file, std::ios::binary | std::ios::app);
        uint32_t magic = WriteAheadLog::kFrameMagic;
        uint32_t fake_crc = 0xDEADBEEFu;
        uint32_t claimed_len = 500; // Claim 500 bytes follow, but only write 10 bytes!
        corrupt_out.write(reinterpret_cast<const char*>(&magic), 4);
        corrupt_out.write(reinterpret_cast<const char*>(&fake_crc), 4);
        corrupt_out.write(reinterpret_cast<const char*>(&claimed_len), 4);
        corrupt_out.write("TORN_WRITE", 10);
    }

    size_t torn_skipped = 0;
    auto recovered = WriteAheadLog::recover(wal_file, &torn_skipped);
    assert(torn_skipped == 1);
    assert(recovered.size() == 5);

    auto state = WriteAheadLog::replay_to_map(recovered);
    assert(state.size() == 5);
    assert(state.at("incident_5").get_int64("severity").value() == 5);

    std::remove(wal_file.c_str());
    std::cout << "  -> Detected and safely skipped " << torn_skipped << " torn tail frame!\n";
    std::cout << "  -> [PASS] Recovered all 5 committed documents with zero data loss!\n\n";
}

void run_wal_benchmark() {
    std::cout << "[BENCHMARK] Running 50,000 WAL Append + CRC32 Checksum Benchmark...\n";
    const std::string wal_file = "test_part2_bench.wal";
    std::remove(wal_file.c_str());

    constexpr size_t kNumRecords = 50000;
    Document sample("wal_bench_doc");
    sample.set("service", "atlas-ingestor")
          .set("epoch_ms", static_cast<int64_t>(1762700000))
          .set("embedding", std::vector<float>(128, 0.125f));

    auto start = std::chrono::high_resolution_clock::now();
    {
        // Use buffered OS writes with explicit flush at end of batch (group commit style)
        WriteAheadLog wal(wal_file, false);
        for (size_t i = 0; i < kNumRecords; ++i) {
            wal.append_insert(sample);
        }
        wal.flush();
    }
    auto end = std::chrono::high_resolution_clock::now();

    double elapsed_sec = std::chrono::duration<double>(end - start).count();
    double ops_per_sec = static_cast<double>(kNumRecords) / elapsed_sec;

    std::cout << "  -> Logged & checksummed " << kNumRecords << " vector documents to disk in "
              << elapsed_sec << " sec\n";
    std::cout << "  -> WAL Throughput: " << static_cast<uint64_t>(ops_per_sec) << " writes/sec\n";

    std::remove(wal_file.c_str());
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  MiniTiger-DB Part 2: Write-Ahead Log & Recovery Suite\n";
    std::cout << "=========================================================\n\n";

    test_wal_replay_and_checkpoint();
    test_torn_write_crash_recovery();
    run_wal_benchmark();

    std::cout << "\nAll Part 2 checks passed!\n";
    return 0;
}