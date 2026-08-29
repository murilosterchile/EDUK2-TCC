#include "../src/optimized/kernel_dispatcher.hpp"
#include "../src/optimized/instance_features.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace ukp;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Instance make_affine(std::size_t n, Weight capacity, Profit offset) {
    Instance instance;
    instance.capacity = capacity;
    instance.items.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Weight weight = 10'000 + static_cast<Weight>((i * 7919) % 10'000);
        instance.items.push_back(
            {static_cast<int>(i), weight, weight + offset});
    }
    return instance;
}

}  // namespace

int main() {
    try {
        const optimized::TsoOptions unlimited;

        InstanceFeatures compact;
        compact.after_common_preprocessing_items = 5000;
        compact.near_best_efficiency_items = 5000;
        compact.capacity = 440'000;
        compact.min_weight = 10'000;
        compact.best_item_weight = 10'100;
        compact.capacity_over_min_weight = 44.0;
        require(optimized::detail::dispatch_kernel(compact, unlimited).kernel ==
                    optimized::detail::KernelChoice::Tso,
                "certified compact region was rejected");

        InstanceFeatures wide_capacity = compact;
        wide_capacity.capacity = 734'000;
        wide_capacity.capacity_over_min_weight = 73.4;
        require(optimized::detail::dispatch_kernel(wide_capacity, unlimited).kernel ==
                    optimized::detail::KernelChoice::Eduk2,
                "wide capacity/min-weight region selected TSO");

        InstanceFeatures heavy_best = compact;
        heavy_best.best_item_weight = 18'000;
        require(optimized::detail::dispatch_kernel(heavy_best, unlimited).kernel ==
                    optimized::detail::KernelChoice::Eduk2,
                "heavy best-efficiency orientation selected TSO");

        InstanceFeatures nonuniform = compact;
        nonuniform.near_best_efficiency_items = 4999;
        require(optimized::detail::dispatch_kernel(nonuniform, unlimited).kernel ==
                    optimized::detail::KernelChoice::Eduk2,
                "non-uniform efficiency region selected TSO");

        InstanceFeatures too_many = compact;
        too_many.after_common_preprocessing_items = 5001;
        too_many.near_best_efficiency_items = 5001;
        require(optimized::detail::dispatch_kernel(too_many, unlimited).kernel ==
                    optimized::detail::KernelChoice::Eduk2,
                "dispatcher selected TSO above item-count limit");

        optimized::TsoOptions tiny_budget;
        tiny_budget.max_transitions = 90'000;
        require(optimized::detail::dispatch_kernel(compact, tiny_budget).kernel ==
                    optimized::detail::KernelChoice::Eduk2,
                "undersized speculation budget selected TSO");

        // p=w+5: near-proportional with the best efficiency at the light end.
        const Instance positive = make_affine(5000, 440'000, 5);
        const auto positive_features = optimized::detail::extract_dispatch_features(
            positive, positive.items);
        require(positive_features.near_best_efficiency_items == 5000,
                "positive strong-correlation shape was not certified");
        require(optimized::detail::dispatch_kernel(positive_features, unlimited).kernel ==
                    optimized::detail::KernelChoice::Tso,
                "positive strong-correlation candidate did not select TSO");

        // p=w-5 has almost the same efficiency spread, but the best item is at
        // the heavy end.  The sample should reject it before a full scan.
        const Instance negative = make_affine(5000, 440'000, -5);
        const auto negative_features = optimized::detail::extract_dispatch_features(
            negative, negative.items);
        require(negative_features.near_best_efficiency_items == 0,
                "negative strong-correlation shape survived certification");
        require(optimized::detail::dispatch_kernel(negative_features, unlimited).kernel ==
                    optimized::detail::KernelChoice::Eduk2,
                "negative strong-correlation shape selected TSO");

        // Equal p/w is detected structurally, not by an SS family name.
        const Instance equal_efficiency = make_affine(5000, 800'000, 0);
        const auto equal_features = optimized::detail::extract_dispatch_features(
            equal_efficiency, equal_efficiency.items);
        require(equal_features.near_best_efficiency_items == 5000,
                "equal-efficiency structure was not certified");
        require(optimized::detail::dispatch_kernel(equal_features, unlimited).kernel ==
                    optimized::detail::KernelChoice::Eduk2,
                "equal-efficiency case outside compact region selected TSO");

        // Large-n instances are an O(1) dispatcher rejection: min_weight stays
        // unset because the item array is never scanned by feature extraction.
        const Instance large = make_affine(10'000, 6'500'000, -5);
        const auto large_features = optimized::detail::extract_dispatch_features(
            large, large.items);
        require(large_features.min_weight == 0,
                "large-n dispatcher rejection unexpectedly scanned items");
        require(optimized::detail::dispatch_kernel(large_features, unlimited).kernel ==
                    optimized::detail::KernelChoice::Eduk2,
                "large-n cheap rejection selected TSO");

        // Deliberately hide one bad item away from the 12 sample locations.
        // Sampling may let it through, but the mandatory full pass must reject.
        Instance adversarial = make_affine(5000, 440'000, 5);
        adversarial.items[123].p /= 2;
        const auto adversarial_features = optimized::detail::extract_dispatch_features(
            adversarial, adversarial.items);
        require(adversarial_features.near_best_efficiency_items == 0,
                "sample-only evidence created a TSO false positive");

        std::cout << "kernel dispatcher conservative-shape checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}