#include "document.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

using namespace minitiger;

void test_roundtrip_serialization() {
    std::cout << "[TEST 1] Running BSON-like Document Round-Trip Serialization Test...\n";

    // Simulate a 384-dimensional AI embedding vector for a telemetry log event
    std::vector<float> embedding(384);
    for (size_t i = 0; i < embedding.size(); ++i) {
        embedding[i] = static_cast<float>(i) * 0.0025f;
    }

    Document original("evt_2026_11_09_001");
    original.set("service_name", "payment-gateway")
            .set("severity_code", 500)
            .set("latency_ms", 142.85)
            .set("is_retriable", true)
            .set("raw_message", "WiredTiger write conflict detected during concurrent checkout transaction")
            .set("embedding", embedding);

    std::vector<uint8_t> binary_payload = original.serialize();
    std::cout << "  -> Serialized Document Size: " << binary_payload.size() << " bytes\n";
    std::cout << "  -> Diagnostic Preview: " << original.to_json_string() << "\n";

    Document decoded = Document::deserialize(binary_payload);

    assert(decoded == original);
    assert(decoded.id() == "evt_2026_11_09_001");
    assert(decoded.get_string("service_name").value() == "payment-gateway");
    assert(decoded.get_int64("severity_code").value() == 500);
    assert(decoded.get_double("latency_ms").value() == 142.85);
    assert(decoded.get_bool("is_retriable").value() == true);
    assert(decoded.get_vector("embedding").value().size() == 384);
    assert(decoded.get_vector("embedding").value()[100] == embedding[100]);

    std::cout << "  -> [PASS] Round-trip binary equality verified!\n\n";
}

void test_corrupted_buffer_protection() {
    std::cout << "[TEST 2] Running Truncated & Corrupted Buffer Protection Test...\n";

    Document doc("evt_corrupt_check");
    doc.set("host", "atlas-shard-01").set("cpu_load", 0.88);
    std::vector<uint8_t> valid_bytes = doc.serialize();

    // Case A: Truncate the buffer mid-stream
    std::vector<uint8_t> truncated(valid_bytes.begin(), valid_bytes.begin() + 12);
    bool caught_truncated = false;
    try {
        (void)Document::deserialize(truncated);
    } catch (const std::runtime_error&) {
        caught_truncated = true;
    }
    assert(caught_truncated);

    // Case B: Corrupt the 4-byte Magic Header
    std::vector<uint8_t> bad_magic = valid_bytes;
    bad_magic[0] = 0xFF;
    bool caught_bad_magic = false;
    try {
        (void)Document::deserialize(bad_magic);
    } catch (const std::runtime_error&) {
        caught_bad_magic = true;
    }
    assert(caught_bad_magic);

    std::cout << "  -> [PASS] Safely rejected corrupted and truncated byte buffers!\n\n";
}

void run_serialization_benchmark() {
    std::cout << "[BENCHMARK] Running 100,000 Document Binary Encode/Decode Benchmark...\n";
    constexpr size_t kIterations = 100000;

    std::vector<float> vec(128, 0.42f);
    Document sample("bench_doc_0001");
    sample.set("service", "query-router")
          .set("status", 200)
          .set("duration_ms", 4.19)
          .set("cached", true)
          .set("embedding", vec);

    auto start = std::chrono::high_resolution_clock::now();
    size_t total_bytes_processed = 0;

    for (size_t i = 0; i < kIterations; ++i) {
        auto buf = sample.serialize();
        auto restored = Document::deserialize(buf);
        total_bytes_processed += buf.size();
        if (restored.id().empty()) {
            std::abort();
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed_sec = std::chrono::duration<double>(end - start).count();
    double docs_per_sec = static_cast<double>(kIterations) / elapsed_sec;
    double mb_per_sec = (static_cast<double>(total_bytes_processed) / (1024.0 * 1024.0)) / elapsed_sec;

    std::cout << "  -> Processed " << kIterations << " documents (with 128-dim float vectors) in "
              << elapsed_sec << " sec\n";
    std::cout << "  -> Throughput: " << static_cast<uint64_t>(docs_per_sec) << " docs/sec ("
              << mb_per_sec << " MB/sec)\n";
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  MiniTiger-DB Part 1: Document & Binary Codec Test Suite\n";
    std::cout << "=========================================================\n\n";

    test_roundtrip_serialization();
    test_corrupted_buffer_protection();
    run_serialization_benchmark();

    std::cout << "\nAll Part 1 checks passed!\n";
    return 0;
}