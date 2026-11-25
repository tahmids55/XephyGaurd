#pragma once
// Non-throwing typed reads for JSON that comes from outside the agent (CCC, helpers, LAN discovery).
// nlohmann's `value()` / `get<T>()` throw json::type_error on a type mismatch, which would terminate the
// root service when it happens on a thread without a handler (I-AGENT-13). These return `def` instead.
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace xg::jr {

using json = nlohmann::json;

inline const json* field(const json& j, std::string_view key) {
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

inline std::string str(const json& j, std::string_view key, std::string def = {}) {
    const json* v = field(j, key);
    return v && v->is_string() ? v->get<std::string>() : def;
}

inline bool boolean(const json& j, std::string_view key, bool def = false) {
    const json* v = field(j, key);
    return v && v->is_boolean() ? v->get<bool>() : def;
}

/// Integer in [lo, hi]; anything else (string, float, out of range) yields `def`.
inline int64_t i64(const json& j, std::string_view key, int64_t def = 0,
                   int64_t lo = std::numeric_limits<int64_t>::min(), int64_t hi = std::numeric_limits<int64_t>::max()) {
    const json* v = field(j, key);
    if (!v) return def;
    if (v->is_number_unsigned()) {
        const auto u = v->get<uint64_t>();
        if (u > uint64_t(std::numeric_limits<int64_t>::max())) return def;
        const auto s = int64_t(u);
        return s < lo || s > hi ? def : s;
    }
    if (v->is_number_integer()) {
        const auto s = v->get<int64_t>();
        return s < lo || s > hi ? def : s;
    }
    return def;
}

/// Any finite number in [lo, hi]; otherwise `def`.
inline double num(const json& j, std::string_view key, double def = 0.0, double lo = -1e18, double hi = 1e18) {
    const json* v = field(j, key);
    if (!v || !v->is_number()) return def;
    const double d = v->get<double>();
    return (d != d || d < lo || d > hi) ? def : d;
}

/// The member if it is an object, else an empty object.
inline const json& obj(const json& j, std::string_view key) {
    static const json empty = json::object();
    const json* v = field(j, key);
    return v && v->is_object() ? *v : empty;
}

}  // namespace xg::jr
