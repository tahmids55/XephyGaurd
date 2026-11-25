#pragma once
// One background thread for slow, self-contained jobs (artifact walks). A job gets copies of what it
// needs, never agent state, and reports back by posting a message to the core (DECISIONS #22).
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace xg {

class Worker {
public:
    using Job = std::function<void(const std::atomic<bool>& stop)>;

    Worker() = default;
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker() { stop(); }

    void start() {
        if (th_.joinable()) return;
        stop_ = false;
        th_ = std::thread([this] { loop(); });
    }

    /// Queues `job` if the worker is idle; returns false while a job is pending or running.
    bool submit(Job job) {
        {
            std::lock_guard lk(mu_);
            if (busy_ || stop_) return false;
            job_ = std::move(job);
            busy_ = true;
        }
        cv_.notify_one();
        return true;
    }

    bool busy() const {
        std::lock_guard lk(mu_);
        return busy_;
    }

    void stop() {
        {
            std::lock_guard lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (th_.joinable()) th_.join();
    }

private:
    void loop() {
        while (true) {
            Job job;
            {
                std::unique_lock lk(mu_);
                cv_.wait(lk, [&] { return stop_.load() || job_.has_value(); });
                if (stop_) return;
                job = std::move(*job_);
                job_.reset();
            }
            try {
                job(stop_);
            } catch (...) {
                // A job reports its own errors; the worker must survive them.
            }
            std::lock_guard lk(mu_);
            busy_ = false;
        }
    }

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::optional<Job> job_;
    bool busy_ = false;
    std::atomic<bool> stop_{false};
    std::thread th_;
};

}  // namespace xg
