#include "ukp/io.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace ukp {

namespace {

std::string_view trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return {};

    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

bool parse_integer_prefix(std::string_view text, long long& value) {
    text = trim(text);
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec == std::errc{} && result.ptr != text.data()) return true;

    // Preserve the stream parser's acceptance of less common spellings, such
    // as a leading plus sign, without paying its allocation cost normally.
    std::istringstream fallback{std::string(text)};
    return static_cast<bool>(fallback >> value);
}

bool parse_pyasukp_item(std::string_view text, Weight& weight, Profit& profit) {
    text = trim(text);
    const std::string_view original = text;
    const auto weight_result =
        std::from_chars(text.data(), text.data() + text.size(), weight);
    if (weight_result.ec == std::errc{} && weight_result.ptr != text.data()) {
        text.remove_prefix(static_cast<std::size_t>(weight_result.ptr - text.data()));
        text = trim(text);
        const std::size_t token_size = text.find_first_of(" \t\r\n");
        const std::string_view token = text.substr(0, token_size);
        Profit integer_profit = 0;
        const auto integer_result =
            std::from_chars(token.data(), token.data() + token.size(), integer_profit);
        constexpr Profit kLargestExactDoubleInteger = Profit{1} << 53;
        if (integer_result.ec == std::errc{} &&
            integer_result.ptr == token.data() + token.size() &&
            integer_profit >= -kLargestExactDoubleInteger &&
            integer_profit <= kLargestExactDoubleInteger) {
            profit = integer_profit;
            return true;
        }

        double floating_profit = 0;
        const auto profit_result = std::from_chars(
            text.data(), text.data() + text.size(), floating_profit);
        if (profit_result.ec == std::errc{} && profit_result.ptr != text.data()) {
            profit = static_cast<Profit>(std::llround(floating_profit));
            return true;
        }
    }

    double floating_profit = 0;
    std::istringstream fallback{std::string(original)};
    if (!(fallback >> weight >> floating_profit)) return false;
    profit = static_cast<Profit>(std::llround(floating_profit));
    return true;
}

void append_legacy_integers(std::string_view text, std::vector<long long>& values) {
    while (true) {
        text = trim(text);
        if (text.empty()) return;

        long long value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec == std::errc{} && result.ptr != text.data()) {
            values.push_back(value);
            text.remove_prefix(static_cast<std::size_t>(result.ptr - text.data()));
            continue;
        }

        std::istringstream fallback{std::string(text)};
        while (fallback >> value) values.push_back(value);
        return;
    }
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
        std::string_view content(line);
        const auto comment_pos = content.find('#');
        if (comment_pos != std::string_view::npos) content = content.substr(0, comment_pos);
        content = trim(content);

        if (content.empty()) continue;

        // -------------------------------------------------------------
        // Detect PYAsUKP/OCaml format
        // -------------------------------------------------------------

        if (content.rfind("n:", 0) == 0) {
            pyasukp_format = true;
            parse_integer_prefix(content.substr(2), expected_n);
            if (expected_n >= 0 &&
                static_cast<unsigned long long>(expected_n) <= inst.items.max_size()) {
                inst.items.reserve(static_cast<std::size_t>(expected_n));
            }

            continue;
        }

        if (content.rfind("c:", 0) == 0) {
            pyasukp_format = true;
            parse_integer_prefix(content.substr(2), inst.capacity);

            continue;
        }

        if (content == "begin data") {
            pyasukp_format = true;
            in_data = true;
            continue;
        }

        if (content == "end data") {
            break;
        }

        // -------------------------------------------------------------
        // PYAsUKP item parsing
        // -------------------------------------------------------------

        if (pyasukp_format && in_data) {
            Weight w;
            Profit p;

            if (!parse_pyasukp_item(content, w, p)) {
                continue;
            }

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
            append_legacy_integers(content, legacy_values);
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
