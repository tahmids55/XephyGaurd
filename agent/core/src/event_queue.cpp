#include "xg/event_queue.hpp"

#include <sqlite3.h>

#include <filesystem>

#include "xg/log.hpp"
#include "xg/util.hpp"

namespace xg {

EventQueue::EventQueue(const std::string& db_path, size_t max_rows) : max_rows_(max_rows) {
    std::error_code ec;
    if (db_path != ":memory:") std::filesystem::create_directories(std::filesystem::path(db_path).parent_path(), ec);
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        log::error("queue: cannot open {}: {}", db_path, db_ ? sqlite3_errmsg(db_) : "?");
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec("CREATE TABLE IF NOT EXISTS events(seq INTEGER PRIMARY KEY, ts INTEGER NOT NULL, type TEXT NOT NULL, data TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS kv(k TEXT PRIMARY KEY, v TEXT NOT NULL)");
    // Sequence survives restarts AND acks (events table may be empty): keep the high-water mark in kv.
    int64_t hw = 0;
    if (auto v = kv_get("seq_high_water")) hw = std::stoll(*v);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COALESCE(MAX(seq),0) FROM events", -1, &st, nullptr) == SQLITE_OK &&
        sqlite3_step(st) == SQLITE_ROW)
        hw = std::max<int64_t>(hw, sqlite3_column_int64(st, 0));
    sqlite3_finalize(st);
    next_seq_ = hw + 1;
}

EventQueue::~EventQueue() {
    if (db_) sqlite3_close(db_);
}

void EventQueue::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        log::error("queue: sql error: {}", err ? err : "?");
        sqlite3_free(err);
    }
}

void EventQueue::emit(const std::string& type, json data) { push(type, data, now_ms()); }

int64_t EventQueue::push(const std::string& type, const json& data, int64_t ts_ms) {
    std::lock_guard lk(mu_);
    if (!db_) return 0;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "INSERT INTO events(seq,ts,type,data) VALUES(?,?,?,?)", -1, &st, nullptr) != SQLITE_OK)
        return 0;
    const int64_t seq = next_seq_;
    const std::string payload = data.dump();
    sqlite3_bind_int64(st, 1, seq);
    sqlite3_bind_int64(st, 2, ts_ms);
    sqlite3_bind_text(st, 3, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, payload.c_str(), int(payload.size()), SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) return 0;
    ++next_seq_;
    if (++pushes_since_cap_ >= 1000) { pushes_since_cap_ = 0; enforce_cap(); }
    return seq;
}

std::vector<Event> EventQueue::peek(size_t limit) {
    std::lock_guard lk(mu_);
    std::vector<Event> out;
    if (!db_) return out;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT seq,ts,type,data FROM events ORDER BY seq LIMIT ?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, int64_t(limit));
    while (sqlite3_step(st) == SQLITE_ROW) {
        Event e;
        e.seq = sqlite3_column_int64(st, 0);
        e.ts_ms = sqlite3_column_int64(st, 1);
        e.type = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
        e.data = json::parse(reinterpret_cast<const char*>(sqlite3_column_text(st, 3)), nullptr, false);
        if (e.data.is_discarded()) e.data = json::object();
        out.push_back(std::move(e));
    }
    sqlite3_finalize(st);
    return out;
}

void EventQueue::ack(int64_t upto) {
    std::lock_guard lk(mu_);
    if (!db_) return;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM events WHERE seq <= ?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, upto);
    sqlite3_step(st);
    sqlite3_finalize(st);
    sqlite3_prepare_v2(db_, "INSERT INTO kv(k,v) VALUES('seq_high_water',?) ON CONFLICT(k) DO UPDATE SET v=excluded.v",
                       -1, &st, nullptr);
    const std::string hw = std::to_string(next_seq_ - 1);
    sqlite3_bind_text(st, 1, hw.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

size_t EventQueue::size() {
    std::lock_guard lk(mu_);
    if (!db_) return 0;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM events", -1, &st, nullptr);
    size_t n = sqlite3_step(st) == SQLITE_ROW ? size_t(sqlite3_column_int64(st, 0)) : 0;
    sqlite3_finalize(st);
    return n;
}

int64_t EventQueue::last_seq() {
    std::lock_guard lk(mu_);
    return next_seq_ - 1;
}

void EventQueue::enforce_cap() {
    // Called with mu_ held. When the CCC has been unreachable for very long, drop the oldest
    // low-value events first (heartbeat-like samples) and keep rule hits and tamper events.
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM events", -1, &st, nullptr);
    size_t n = sqlite3_step(st) == SQLITE_ROW ? size_t(sqlite3_column_int64(st, 0)) : 0;
    sqlite3_finalize(st);
    if (n <= max_rows_) return;
    const auto excess = int64_t(n - max_rows_);
    sqlite3_prepare_v2(db_,
                       "DELETE FROM events WHERE seq IN (SELECT seq FROM events WHERE type IN "
                       "('key_stats','gpu_sample','window_focus','file_activity','process_snapshot') ORDER BY seq LIMIT ?)",
                       -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, excess);
    sqlite3_step(st);
    sqlite3_finalize(st);
    log::warn("queue: cap reached, dropped up to {} low-value events", excess);
}

void EventQueue::kv_set(const std::string& k, const std::string& v) {
    std::lock_guard lk(mu_);
    if (!db_) return;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO kv(k,v) VALUES(?,?) ON CONFLICT(k) DO UPDATE SET v=excluded.v", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, k.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, v.c_str(), int(v.size()), SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::optional<std::string> EventQueue::kv_get(const std::string& k) {
    std::lock_guard lk(mu_);
    if (!db_) return std::nullopt;
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT v FROM kv WHERE k=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, k.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<std::string> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
    return out;
}

}  // namespace xg
