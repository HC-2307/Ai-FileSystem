// Workload generator and measurement harness. Builds a fresh filesystem,
// replays one workload against one version, and prints one CSV row.
//
//   ai_fs_bench <A|B|C> <sequential|random|repetitive|mixed> [cacheKb] [thinkUs] [seed]
//   ai_fs_bench header
//
// It calls the filesystem library directly rather than going through a mount,
// so the numbers compare the storage policies without FUSE's fixed overhead.
// The image is opened with O_DIRECT: every cache miss is a real device read.
#include "filesystem.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

const std::size_t BS = block_storage::BLOCK_SIZE;
const int FILES = 8;
const int BLOCKS_PER_FILE = 512;    // 2 MiB each, 16 MiB of data
const int OPS = 8192;
const int PATTERN = 600;            // length of the repeating order; longer than the cache holds

struct op {
    int file, block;
    bool isWrite;
};

std::string name(int file) {
    return "/f" + std::to_string(file);
}

void sequential(std::vector<op>& ops, int count, int& file, int& block) {
    for (int i = 0; i < count; i++) {
        ops.push_back(op{file, block, false});
        if (++block == BLOCKS_PER_FILE) {
            block = 0;
            file = (file + 1) % FILES;
        }
    }
}

void random(std::vector<op>& ops, int count, std::mt19937& rng) {
    for (int i = 0; i < count; i++)
        ops.push_back(op{int(rng() % FILES), int(rng() % BLOCKS_PER_FILE), false});
}

void repetitive(std::vector<op>& ops, int count, const std::vector<op>& pattern, int& at) {
    for (int i = 0; i < count; i++) ops.push_back(pattern[at++ % pattern.size()]);
}

std::vector<op> build(const std::string& workload, std::mt19937& rng) {
    std::vector<op> pattern, ops;
    random(pattern, PATTERN, rng);
    int file = 0, block = 0, at = 0;
    if (workload == "sequential") {
        sequential(ops, OPS, file, block);
    } else if (workload == "random") {
        random(ops, OPS, rng);
    } else if (workload == "repetitive") {
        repetitive(ops, OPS, pattern, at);
    } else if (workload == "mixed") {
        // The pattern changes every 512 operations, and one in ten is a write.
        for (int phase = 0; int(ops.size()) < OPS; phase++) {
            if (phase % 3 == 0) sequential(ops, 512, file, block);
            else if (phase % 3 == 1) repetitive(ops, 512, pattern, at);
            else random(ops, 512, rng);
        }
        for (op& o : ops) o.isWrite = rng() % 10 == 0;
    } else {
        throw std::invalid_argument("unknown workload " + workload);
    }
    return ops;
}

double percentile(std::vector<double>& sorted, double p) {
    return sorted.empty() ? 0 : sorted[std::size_t(p * (sorted.size() - 1))];
}

double mean(const std::vector<double>& v) {
    double sum = 0;
    for (double x : v) sum += x;
    return v.empty() ? 0 : sum / v.size();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "header") {
        std::cout << "version,workload,cache_kb,think_us,seed,ops,throughput_ops_s,read_avg_us,read_p50_us,"
                     "read_p99_us,write_avg_us,cache_hit_rate,prefetch_issued,prefetch_used,prefetch_wasted,"
                     "prediction_precision,prediction_coverage,disk_reads,disk_writes,memory_kb,"
                     "index_queries,index_time_ms,final_depth,final_threshold,final_data_share\n";
        return 0;
    }
    if (argc < 3) {
        std::cerr << "usage: ai_fs_bench <A|B|C> <sequential|random|repetitive|mixed> [cacheKb] [thinkUs] [seed]\n";
        return 2;
    }
    try {
        const std::string version = argv[1], workload = argv[2];
        const long cacheKb = argc > 3 ? std::atol(argv[3]) : 1024;
        const long thinkUs = argc > 4 ? std::atol(argv[4]) : 0;
        const unsigned seed = argc > 5 ? unsigned(std::atol(argv[5])) : 1;
        if (version != "A" && version != "B" && version != "C") throw std::invalid_argument("unknown version");

        std::mt19937 rng(seed);
        const std::vector<op> ops = build(workload, rng);

        const std::string stem = "bench_" + std::to_string(getpid());
        const std::string image = stem + ".img", db = stem + ".db";
        fs_options options;
        options.mode = io_mode::direct;
        options.cacheBytes = std::size_t(cacheKb) * 1024;
        options.version = fs_version(version[0] - 'A');

        filesystem fs;
        fs.format(image, db, FILES * BLOCKS_PER_FILE * 2, options);
        for (int f = 0; f < FILES; f++) {
            fs.createFile(name(f));
            fs.write(name(f), 0, std::vector<char>(BLOCKS_PER_FILE * BS, char('a' + f)));
        }
        fs.close();
        fs.open(image, db, options);   // empty caches, zeroed statistics

        std::vector<double> readUs, writeUs;
        const std::vector<char> payload(BS, 'w');
        for (const op& o : ops) {
            const auto start = std::chrono::steady_clock::now();
            if (o.isWrite) fs.write(name(o.file), std::uint64_t(o.block) * BS, payload);
            else fs.read(name(o.file), std::uint64_t(o.block) * BS, BS);
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            (o.isWrite ? writeUs : readUs).push_back(us);
            // The time an application spends on the data before asking for more.
            if (thinkUs > 0) std::this_thread::sleep_for(std::chrono::microseconds(thinkUs));
        }

        const fs_stats s = fs.stats();
        fs.close();
        std::remove(image.c_str());
        std::remove(db.c_str());

        double busyUs = 0;
        for (double x : readUs) busyUs += x;
        for (double x : writeUs) busyUs += x;
        const double readAvg = mean(readUs), writeAvg = mean(writeUs);
        std::sort(readUs.begin(), readUs.end());
        const std::uint64_t lookups = s.cache.hits + s.cache.misses;
        const std::uint64_t settled = s.cache.prefetchUsed + s.cache.prefetchWasted;

        std::printf("%s,%s,%ld,%ld,%u,%zu,%.0f,%.2f,%.2f,%.2f,%.2f,%.4f,%llu,%llu,%llu,%.4f,%.4f,%llu,%llu,%llu,%llu,%.2f,%d,%.2f,%.2f\n",
                    version.c_str(), workload.c_str(), cacheKb, thinkUs, seed, ops.size(),
                    ops.size() / (busyUs / 1e6),   // operations per second of time spent inside the filesystem
                    readAvg, percentile(readUs, 0.5), percentile(readUs, 0.99), writeAvg,
                    lookups ? double(s.cache.hits) / lookups : 0.0,
                    (unsigned long long)s.cache.prefetchIssued, (unsigned long long)s.cache.prefetchUsed,
                    (unsigned long long)s.cache.prefetchWasted,
                    settled ? double(s.cache.prefetchUsed) / settled : 0.0,   // of the prefetches whose fate is known
                    lookups ? double(s.cache.prefetchUsed) / lookups : 0.0,   // block lookups served by a prefetch
                    (unsigned long long)s.cache.diskReads, (unsigned long long)s.cache.diskWrites,
                    (unsigned long long)(s.memoryBytes / 1024), (unsigned long long)s.dbLookups,
                    s.dbLookupNanos / 1e6, s.depth, s.threshold, s.dataShare);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ai_fs_bench: " << e.what() << std::endl;
        return 1;
    }
}
