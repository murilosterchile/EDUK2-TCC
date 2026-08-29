#include "../src/optimized/kernel_dispatcher.hpp"
#include "../src/optimized/instance_features.hpp"

#include <iostream>
#include <stdexcept>

using namespace ukp;

int main() {
    try {
        InstanceFeatures features;
        features.after_common_preprocessing_items = 5000;
        features.near_best_efficiency_items = 5000;
        features.capacity = 150'000;
        features.min_weight = 10'000;
        features.capacity_over_min_weight = 15.0;

        const optimized::TsoOptions options;
        const auto at_boundary =
            optimized::detail::dispatch_kernel(features, options);
        if (at_boundary.kernel != optimized::detail::KernelChoice::Tso) {
            throw std::runtime_error("TSO conservative boundary was rejected");
        }

        features.capacity = 150'001;
        features.capacity_over_min_weight = 15.0001;
        const auto above_capacity_ratio =
            optimized::detail::dispatch_kernel(features, options);
        if (above_capacity_ratio.kernel != optimized::detail::KernelChoice::Eduk2) {
            throw std::runtime_error("TSO selected above conservative capacity ratio");
        }

        features.capacity = 1'000'000;
        features.min_weight = 66'667;
        features.capacity_over_min_weight = 14.9999;
        const auto above_work =
            optimized::detail::dispatch_kernel(features, options);
        if (above_work.kernel != optimized::detail::KernelChoice::Eduk2) {
            throw std::runtime_error("TSO selected above conservative work limit");
        }

        const Instance rejected{1'000'000,
                                {{0, 10'000, 20'000}, {1, 11'000, 21'000}}};
        const auto cheap_rejection = optimized::detail::extract_dispatch_features(
            rejected, rejected.items);
        if (optimized::detail::dispatch_kernel(cheap_rejection, options).kernel !=
            optimized::detail::KernelChoice::Eduk2) {
            throw std::runtime_error("cheap feature prefilter selected TSO");
        }

        const Instance candidate{120'000,
                                 {{0, 10'000, 20'000}, {1, 11'000, 22'000}}};
        const auto candidate_features =
            optimized::detail::extract_dispatch_features(candidate, candidate.items);
        if (optimized::detail::dispatch_kernel(candidate_features, options).kernel !=
            optimized::detail::KernelChoice::Tso) {
            throw std::runtime_error("cheap feature prefilter rejected TSO candidate");
        }

        std::cout << "kernel dispatcher boundaries passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
