#include "ukp/io.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace ukp {

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";

    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

}  // namespace

Instance read_instance(std::istream& in) {
    Instance inst;

    std::string line;

    bool pyasukp_format = false;
    bool in_data = false;

    long long expected_n = -1;

    int next_id = 0;

    // ---------------------------------------------------------------------
    // First pass:
    // Detect whether the file is in:
    //
    // 1. Old compact format:
    //      n capacity
    //      p w
    //      p w
    //
    // 2. PYAsUKP/OCaml format:
    //      n: ...
    //      c: ...
    //      begin data
    //      w p
    //      end data
    // ---------------------------------------------------------------------

    std::vector<long long> legacy_values;

    while (std::getline(in, line)) {
        auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        line = trim(line);

        if (line.empty()) continue;

        // -------------------------------------------------------------
        // Detect PYAsUKP/OCaml format
        // -------------------------------------------------------------

        if (line.rfind("n:", 0) == 0) {
            pyasukp_format = true;

            std::stringstream ss(line.substr(2));
            ss >> expected_n;

            continue;
        }

        if (line.rfind("c:", 0) == 0) {
            pyasukp_format = true;

            std::stringstream ss(line.substr(2));
            ss >> inst.capacity;

            continue;
        }

        if (line == "begin data") {
            pyasukp_format = true;
            in_data = true;
            continue;
        }

        if (line == "end data") {
            break;
        }

        // -------------------------------------------------------------
        // PYAsUKP item parsing
        // -------------------------------------------------------------

        if (pyasukp_format && in_data) {
            std::stringstream ss(line);

            Weight w;
            double p_double;

            if (!(ss >> w >> p_double)) {
                continue;
            }

            Profit p =
                static_cast<Profit>(std::llround(p_double));

            if (p <= 0 || w <= 0) {
                throw std::runtime_error(
                    "items must have positive profit and weight");
            }

            inst.items.push_back(
                Item{next_id++, w, p});

            continue;
        }

        // -------------------------------------------------------------
        // Legacy parser fallback
        // -------------------------------------------------------------

        if (!pyasukp_format) {
            std::stringstream ss(line);

            long long x;

            while (ss >> x) {
                legacy_values.push_back(x);
            }
        }
    }

    // -----------------------------------------------------------------
    // Finalize legacy format
    // -----------------------------------------------------------------

    if (!pyasukp_format) {
        if (legacy_values.size() < 2) {
            throw std::runtime_error(
                "invalid instance: expected n and capacity");
        }

        const long long n = legacy_values[0];
        inst.capacity = legacy_values[1];

        if (n < 0 || inst.capacity < 0) {
            throw std::runtime_error(
                "invalid negative n or capacity");
        }

        if (static_cast<size_t>(2 + 2 * n) >
            legacy_values.size()) {
            throw std::runtime_error(
                "invalid instance: expected n pairs p w after header");
        }

        inst.items.reserve(static_cast<size_t>(n));

        size_t k = 2;

        for (long long i = 0; i < n; ++i) {
            Profit p = legacy_values[k++];
            Weight w = legacy_values[k++];

            if (p <= 0 || w <= 0) {
                throw std::runtime_error(
                    "items must have positive profit and weight");
            }

            inst.items.push_back(
                Item{static_cast<int>(i), w, p});
        }

        return inst;
    }

    // -----------------------------------------------------------------
    // Validate PYAsUKP format
    // -----------------------------------------------------------------

    if (inst.capacity <= 0) {
        throw std::runtime_error(
            "invalid or missing capacity");
    }

    if (expected_n >= 0 &&
        static_cast<long long>(inst.items.size()) != expected_n) {
        throw std::runtime_error(
            "number of items does not match n:");
    }

    return inst;
}

Instance read_instance_file(const std::string& path) {
    std::ifstream in(path);

    if (!in) {
        throw std::runtime_error(
            "cannot open input file: " + path);
    }

    return read_instance(in);
}

void write_solution(std::ostream& out, const Solution& sol) {
    out << "solver " << sol.solver_name << '\n';
    out << "optimal " << (sol.optimal ? 1 : 0) << '\n';
    out << "profit " << sol.profit << '\n';
    out << "weight " << sol.weight << '\n';

    out << "multiplicities";

    for (size_t i = 0;
         i < sol.multiplicity_by_id.size();
         ++i) {
        if (sol.multiplicity_by_id[i] != 0) {
            out << ' '
                << i
                << ':'
                << sol.multiplicity_by_id[i];
        }
    }

    out << '\n';
}

}  // namespace ukp
