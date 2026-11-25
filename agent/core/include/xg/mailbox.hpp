#pragma once
// Primitives for the single-threaded actor core (docs/DECISIONS.md "agent actor core").
// Producers (platform watchers, uplink, realtime, workers) only post messages; the core thread owns
// all agent state and is the only consumer. Data flows back to I/O threads through Snapshot<T>.
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace xg {

/// Bounded multi-producer, single-consumer FIFO. `post` never blocks: when full, the message is dropped
/// and counted, so a flooding producer cannot stall a platform thread or exhaust memory.
template <class T>
class Mailbox {
public:
    explicit Mailbox(size_t capacity = 8192) : capacity_(capacity) {}

    /// Returns false (and counts a drop) when the mailbox is full or closed.
    bool post(T m) {
        {
            std::lock_guard lk(mu_);
            if (closed_ || q_.size() >= capacity_) {
                ++dropped_;
                return false;
            }
            q_.push_back(std::move(m));
        }
        cv_.notify_one();
        return true;
    }

    /// Waits up to `timeout` for at least one message, then takes everything queued (FIFO order).
    std::vector<T> wait_take_all(std::chrono::milliseconds timeout) {
        std::unique_lock lk(mu_);
        cv_.wait_for(lk, timeout, [&] { return !q_.empty() || closed_ || woken_; });
        woken_ = false;
        std::vector<T> out;
        out.reserve(q_.size());
        while (!q_.empty()) {
            out.push_back(std::move(q_.front()));
            q_.pop_front();
        }
        return out;
    }

    /// Takes everything queued without waiting.
    std::vector<T> take_all() { return wait_take_all(std::chrono::milliseconds(0)); }

    /// Wakes a waiting consumer without a message (e.g. on shutdown).
    void wake() {
        {
            std::lock_guard lk(mu_);
            woken_ = true;
        }
        cv_.notify_all();
    }

    /// Stops accepting messages; queued ones can still be taken.
    void close() {
        {
            std::lock_guard lk(mu_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    size_t size() const {
        std::lock_guard lk(mu_);
        return q_.size();
    }
    uint64_t dropped() const {
        std::lock_guard lk(mu_);
        return dropped_;
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<T> q_;
    size_t capacity_;
    uint64_t dropped_ = 0;
    bool closed_ = false;
    bool woken_ = false;
};

/// Latest immutable value published by one thread and read by others. Readers get a shared_ptr to a
/// const copy, so they never see a value change underneath them.
template <class T>
class Snapshot {
public:
    Snapshot() : v_(std::make_shared<const T>()) {}
    void set(T v) {
        auto p = std::make_shared<const T>(std::move(v));
        std::lock_guard lk(mu_);
        v_ = std::move(p);
    }
    std::shared_ptr<const T> get() const {
        std::lock_guard lk(mu_);
        return v_;
    }

private:
    mutable std::mutex mu_;
    std::shared_ptr<const T> v_;
};

}  // namespace xg
