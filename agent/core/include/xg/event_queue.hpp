#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "xg/event.hpp"

struct sqlite3;

namespace xg {

/// Durable FIFO of events backed by SQLite (WAL). Sequence numbers are monotonic across
/// restarts; the CCC acknowledges the highest sequence it stored and de-duplicates by
/// (pc_id, boot-independent seq), so re-sending after a crash is harmless.
class EventQueue final : public IEventSink {
public:
    explicit EventQueue(const std::string& db_path, size_t max_rows = 500000);
    ~EventQueue() override;
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    bool ok() const { return db_ != nullptr; }
    void emit(const std::string& type, json data) override;
    /// Appends and returns the assigned sequence number (0 on failure).
    int64_t push(const std::string& type, const json& data, int64_t ts_ms);
    /// Oldest `limit` events not yet acknowledged.
    std::vector<Event> peek(size_t limit);
    /// Removes all events with seq <= upto.
    void ack(int64_t upto);
    size_t size();
    int64_t last_seq();
    /// Key/value persistence for small agent state (cached policy, boot counters ...).
    void kv_set(const std::string& key, const std::string& value);
    std::optional<std::string> kv_get(const std::string& key);

private:
    void exec(const char* sql);
    void enforce_cap();
    sqlite3* db_ = nullptr;
    std::mutex mu_;
    int64_t next_seq_ = 1;
    size_t max_rows_;
    size_t pushes_since_cap_ = 0;
};

/// In-memory sink for tests and the helper.
class VectorSink final : public IEventSink {
public:
    void emit(const std::string& type, json data) override {
        std::lock_guard lk(mu);
        events.push_back(Event{static_cast<int64_t>(events.size() + 1), 0, type, std::move(data)});
    }
    std::vector<Event> events;
    std::mutex mu;
};

}  // namespace xg
