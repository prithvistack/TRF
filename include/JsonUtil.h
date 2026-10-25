#pragma once
#include <string>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace TRF {

// The project deliberately avoids a JSON library ("no JSON library" in the
// spec), so JSON responses are built by hand as plain strings. The one
// piece of real logic needed for that is escaping: a string value being
// embedded inside a JSON string literal must have its quotes, backslashes
// and control characters escaped, or it would produce broken/invalid JSON
// (or, worse, let a borrower's name inject arbitrary JSON).
inline std::string escapeJson(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Other control characters: emit as \u00XX.
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Wraps a raw string value in quotes with escaping applied - the common
// case when building a JSON field like "name":"...".
inline std::string jsonString(const std::string& value) {
    return "\"" + escapeJson(value) + "\"";
}

// Formats a currency amount with exactly two decimal places, as a bare
// JSON number (no quotes) - e.g. 10 -> "10.00". std::to_string(double)
// always prints six decimals, which reads badly for a rupee amount.
inline std::string jsonMoney(double amount) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << amount;
    return oss.str();
}

} // namespace TRF
