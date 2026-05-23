#include "ukp/generator.hpp"
#include <algorithm>
#include <random>
#include <set>

namespace ukp {

Instance make_strongly_correlated(int n, Weight wmin, Profit alpha, Weight capacity) {
    Instance inst;
    inst.capacity = capacity;
    for (int i = 0; i < n; ++i) {
        Weight w = wmin + i;
        Profit p = w + alpha;
        if (p <= 0) p = 1;
        inst.items.push_back({i, w, p});
    }
    return inst;
}

Instance make_random_instance(int n, Weight wmin, Weight wmax, Weight capacity, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<Weight> dw(wmin, wmax);
    Instance inst;
    inst.capacity = capacity;
    for (int i = 0; i < n; ++i) {
        Weight w = dw(rng);
        Profit p = 1 + static_cast<Profit>(dw(rng));
        inst.items.push_back({i, w, p});
    }
    return inst;
}

Instance make_saw_like(int n, Weight wmin, Weight wmax, Weight capacity, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::set<Weight> ws;
    while (static_cast<int>(ws.size()) < n) {
        ws.insert(std::uniform_int_distribution<Weight>(wmin, wmax)(rng));
    }
    Instance inst;
    inst.capacity = capacity;
    std::vector<Weight> weights(ws.begin(), ws.end());
    Weight w1 = weights.front();
    Profit p1 = w1 + 1 + static_cast<Profit>(seed % 5);
    inst.items.push_back({0, w1, p1});
    Profit prev_p = p1;
    for (int i = 1; i < n; ++i) {
        Weight wi = weights[static_cast<size_t>(i)];
        Weight mi = wi % w1;
        Weight ai = wi / w1;
        Profit lo = std::max(prev_p + 1, p1 * ai + 1);
        Profit hi = mi + p1 * ai;
        if (hi < lo) hi = lo;
        Profit pi = std::uniform_int_distribution<Profit>(lo, hi)(rng);
        inst.items.push_back({i, wi, pi});
        prev_p = pi;
    }
    std::shuffle(inst.items.begin(), inst.items.end(), rng);
    for (int i = 0; i < n; ++i) inst.items[static_cast<size_t>(i)].id = i;
    return inst;
}

}  // namespace ukp
