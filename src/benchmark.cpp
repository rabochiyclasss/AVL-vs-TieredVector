// ============================================================================
//  Build:   make           
//  Run:     ./benchmark                 # default N = 10k, 100k, 1M
//           ./benchmark 50000 500000    # custom list of N values
//
//  Workloads (per the assignment):
//    1. Append sequential  : push_back N elements (index == size each time).
//    2. Append random      : insert N elements at RANDOM positions.
//    3. Random access       : M random get(index) calls on a structure of N.
//    4a. Front mutation     : N insert at front (index 0) then N erase front.
//    4b. Random mutation    : N random inserts then N random erases.
// ============================================================================

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "AVLTree.cpp"
#include "tiered_vector.cpp"

// ---------------------------------------------------------------------------
//  Timing helper: run a callable and return elapsed milliseconds (double).
// ---------------------------------------------------------------------------
using Clock = std::chrono::steady_clock;

template <typename Fn>
double timeMs(Fn&& fn) {
    auto t0 = Clock::now();
    fn();
    auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// ---------------------------------------------------------------------------
//  A deterministic RNG so every run is reproducible.
// ---------------------------------------------------------------------------
static std::mt19937_64 g_rng(0xC0FFEE);

// Generate `m` values where the k-th value is uniform in [0, sizes[k]).
// For mutation workloads the valid index range grows/shrinks as we go, so the
// caller passes the size that will be in effect at step k.
static std::vector<uint32_t> genIndicesGrowing(std::size_t m, std::size_t startSize) {
    std::vector<uint32_t> v(m);
    std::size_t curSize = startSize;
    for (std::size_t k = 0; k < m; ++k) {
        std::uniform_int_distribution<std::size_t> d(0, curSize); // inclusive: allow append at end
        v[k] = static_cast<uint32_t>(d(g_rng));
        ++curSize;                       // an insert just happened
    }
    return v;
}

static std::vector<uint32_t> genIndicesShrinking(std::size_t m, std::size_t startSize) {
    std::vector<uint32_t> v(m);
    std::size_t curSize = startSize;
    for (std::size_t k = 0; k < m; ++k) {
        std::uniform_int_distribution<std::size_t> d(0, curSize - 1); // valid erase index
        v[k] = static_cast<uint32_t>(d(g_rng));
        --curSize;                       // an erase just happened
    }
    return v;
}

static std::vector<uint32_t> genIndicesFixed(std::size_t m, std::size_t size) {
    std::vector<uint32_t> v(m);
    std::uniform_int_distribution<std::size_t> d(0, size - 1);
    for (std::size_t k = 0; k < m; ++k) v[k] = static_cast<uint32_t>(d(g_rng));
    return v;
}

// ---------------------------------------------------------------------------
//  Estimated per-allocation malloc overhead (header + alignment rounding).
// ---------------------------------------------------------------------------
constexpr std::size_t MALLOC_OVERHEAD = 16;

// One row of results for the CSV.
struct Row {
    std::string workload;
    std::string structure;
    std::size_t N;
    double      timeMs;
    double      memMB;     // -1 if not applicable
};
std::vector<Row> g_rows;

static void record(const std::string& workload, const std::string& structure,
                   std::size_t N, double ms, double memMB = -1.0) {
    g_rows.push_back({workload, structure, N, ms, memMB});
    std::printf("  %-22s %-14s N=%-9zu %10.3f ms",
                workload.c_str(), structure.c_str(), N, ms);
    if (memMB >= 0) std::printf("   mem=%8.2f MB", memMB);
    std::printf("\n");
}

// ===========================================================================
//  Workload 1: sequential append (push_back).
// ===========================================================================
static void benchAppendSequential(std::size_t N) {
    {
        TieredVector<uint64_t> tv;
        double ms = timeMs([&] { for (std::size_t i = 0; i < N; ++i) tv.push_back(i); });
        double memMB = tv.memoryBytes() / (1024.0 * 1024.0);
        record("append-seq", "TieredVector", N, ms, memMB);
    }
    {
        AVLTree<uint64_t> avl;
        double ms = timeMs([&] { for (std::size_t i = 0; i < N; ++i) avl.push_back(i); });
        double memMB = (avl.memoryBytes() + avl.size() * MALLOC_OVERHEAD) / (1024.0 * 1024.0);
        record("append-seq", "AVLTree", N, ms, memMB);
    }
}

// ===========================================================================
//  Workload 2: append N elements at RANDOM positions.
// ===========================================================================
static void benchAppendRandom(std::size_t N) {
    std::vector<uint32_t> idx = genIndicesGrowing(N, 0); // positions to insert at
    {
        TieredVector<uint64_t> tv;
        double ms = timeMs([&] { for (std::size_t i = 0; i < N; ++i) tv.insert(idx[i], i); });
        double memMB = tv.memoryBytes() / (1024.0 * 1024.0);
        record("append-random", "TieredVector", N, ms, memMB);
    }
    {
        AVLTree<uint64_t> avl;
        double ms = timeMs([&] { for (std::size_t i = 0; i < N; ++i) avl.insert(idx[i], i); });
        double memMB = (avl.memoryBytes() + avl.size() * MALLOC_OVERHEAD) / (1024.0 * 1024.0);
        record("append-random", "AVLTree", N, ms, memMB);
    }
}

// ===========================================================================
//  Workload 3: M random get(index) lookups on a structure already holding N.
//  We fill with sequential appends (fast) and then only time the lookups.
// ===========================================================================
static void benchRandomAccess(std::size_t N, std::size_t M) {
    std::vector<uint32_t> idx = genIndicesFixed(M, N);
    {
        TieredVector<uint64_t> tv;
        for (std::size_t i = 0; i < N; ++i) tv.push_back(i);
        volatile uint64_t sink = 0;      // volatile prevents the compiler from
        double ms = timeMs([&] {         // optimizing the whole loop away
            for (std::size_t i = 0; i < M; ++i) sink ^= tv.get(idx[i]);
        });
        (void)sink;
        record("random-access", "TieredVector", N, ms);
    }
    {
        AVLTree<uint64_t> avl;
        for (std::size_t i = 0; i < N; ++i) avl.push_back(i);
        volatile uint64_t sink = 0;
        double ms = timeMs([&] {
            for (std::size_t i = 0; i < M; ++i) sink ^= avl.get(idx[i]);
        });
        (void)sink;
        record("random-access", "AVLTree", N, ms);
    }
}

// ===========================================================================
//  Workload 4a: front mutation. Insert N elements at index 0, then erase all N
//  from index 0.  This is the worst case for a plain vector and a good stress
//  test for both structures' front handling.
// ===========================================================================
static void benchFrontMutation(std::size_t N) {
    {
        TieredVector<uint64_t> tv;
        double ms = timeMs([&] {
            for (std::size_t i = 0; i < N; ++i) tv.push_front(i);
            for (std::size_t i = 0; i < N; ++i) tv.erase(0);
        });
        record("front-mutation", "TieredVector", N, ms);
    }
    {
        AVLTree<uint64_t> avl;
        double ms = timeMs([&] {
            for (std::size_t i = 0; i < N; ++i) avl.push_front(i);
            for (std::size_t i = 0; i < N; ++i) avl.erase(0);
        });
        record("front-mutation", "AVLTree", N, ms);
    }
}

// ===========================================================================
//  Workload 4b: random mutation. Insert N elements at random positions, then
//  erase N elements at random positions back down to empty.
// ===========================================================================
static void benchRandomMutation(std::size_t N) {
    std::vector<uint32_t> ins = genIndicesGrowing(N, 0);
    std::vector<uint32_t> del = genIndicesShrinking(N, N);
    {
        TieredVector<uint64_t> tv;
        double ms = timeMs([&] {
            for (std::size_t i = 0; i < N; ++i) tv.insert(ins[i], i);
            for (std::size_t i = 0; i < N; ++i) tv.erase(del[i]);
        });
        record("random-mutation", "TieredVector", N, ms);
    }
    {
        AVLTree<uint64_t> avl;
        double ms = timeMs([&] {
            for (std::size_t i = 0; i < N; ++i) avl.insert(ins[i], i);
            for (std::size_t i = 0; i < N; ++i) avl.erase(del[i]);
        });
        record("random-mutation", "AVLTree", N, ms);
    }
}

// ===========================================================================
//  Correctness self-test: run a long sequence of identical random operations
//  on both structures AND a reference std::vector, and assert they always
//  agree.  If this passes we trust the timing numbers.
// ===========================================================================
static bool selfTest() {
    std::mt19937_64 rng(12345);
    std::vector<uint64_t> ref;
    TieredVector<uint64_t> tv;
    AVLTree<uint64_t> avl;

    auto checkAll = [&]() -> bool {
        if (tv.size() != ref.size() || avl.size() != ref.size()) return false;
        for (std::size_t i = 0; i < ref.size(); ++i)
            if (tv.get(i) != ref[i] || avl.get(i) != ref[i]) return false;
        return true;
    };

    for (int step = 0; step < 20000; ++step) {
        // 60% insert, 40% erase (when non-empty), to grow then churn.
        bool doInsert = ref.empty() || (rng() % 100) < 60;
        if (doInsert) {
            std::size_t pos = ref.empty() ? 0 : rng() % (ref.size() + 1);
            uint64_t val = rng();
            ref.insert(ref.begin() + pos, val);
            tv.insert(pos, val);
            avl.insert(pos, val);
        } else {
            std::size_t pos = rng() % ref.size();
            ref.erase(ref.begin() + pos);
            tv.erase(pos);
            avl.erase(pos);
        }
        if (step % 500 == 0 && !checkAll()) {
            std::printf("SELF-TEST FAILED at step %d (size=%zu)\n", step, ref.size());
            return false;
        }
    }
    bool ok = checkAll();
    std::printf("Self-test %s (final size = %zu, AVL height = %d)\n",
                ok ? "PASSED" : "FAILED", ref.size(), avl.height());
    return ok;
}

// ---------------------------------------------------------------------------
//  Write all collected rows to results.csv.
// ---------------------------------------------------------------------------
static void writeCsv(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) { std::perror("fopen results.csv"); return; }
    std::fprintf(f, "workload,structure,N,time_ms,mem_MB\n");
    for (const auto& r : g_rows)
        std::fprintf(f, "%s,%s,%zu,%.6f,%.6f\n",
                     r.workload.c_str(), r.structure.c_str(), r.N, r.timeMs, r.memMB);
    std::fclose(f);
    std::printf("\nWrote %zu rows to %s\n", g_rows.size(), path.c_str());
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);

    // Parse N values from the command line, or use the assignment defaults.
    std::vector<std::size_t> Ns;
    for (int i = 1; i < argc; ++i) Ns.push_back(std::stoull(argv[i]));
    if (Ns.empty()) Ns = {10000, 100000, 1000000};

    std::printf("=== Correctness self-test ===\n");
    if (!selfTest()) { std::printf("Aborting: implementation is incorrect.\n"); return 1; }

    std::printf("\n=== Benchmarks ===\n");
    std::printf("Node size (AVL) = %zu bytes, +%zu bytes/node est. malloc overhead\n\n",
                AVLTree<uint64_t>::nodeSize(), MALLOC_OVERHEAD);

    for (std::size_t N : Ns) {
        std::printf("--- N = %zu ---\n", N);
        // M random lookups: scale with N but cap so the 1M run stays quick.
        std::size_t M = (N < 1000000) ? N : 1000000;

        benchAppendSequential(N);
        benchAppendRandom(N);
        benchRandomAccess(N, M);
        benchFrontMutation(N);
        benchRandomMutation(N);
        std::printf("\n");
    }

    writeCsv("results.csv");
    return 0;
};
