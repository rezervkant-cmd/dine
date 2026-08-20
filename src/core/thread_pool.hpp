#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <vector>

namespace vw {

class ThreadPool {
public:
    explicit ThreadPool(unsigned n = std::thread::hardware_concurrency()) {
        if (n == 0) n = 4;
        workers_.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            workers_.emplace_back([this](std::stop_token st) { run(st); });
    }
    ~ThreadPool() {
        for (auto& w : workers_) w.request_stop();
        cv_.notify_all();

    }

    template <class F, class... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        {
            std::lock_guard lk(m_);
            queue_.emplace_back([task] { (*task)(); });
        }
        cv_.notify_one();
        return task->get_future();
    }

    template <class F>
    void parallel_for(size_t begin, size_t end, F&& body, size_t grain = 1) {
        const size_t n = end - begin;
        if (n == 0) return;
        const size_t nw = workers_.size();
        const size_t chunk = std::max(grain, (n + nw - 1) / nw);
        std::vector<std::future<void>> futs;
        for (size_t s = begin; s < end; s += chunk) {
            size_t e = std::min(s + chunk, end);
            futs.push_back(submit([s, e, &body] { for (size_t i = s; i < e; ++i) body(i); }));
        }
        for (auto& f : futs) f.get();
    }

    unsigned size() const { return static_cast<unsigned>(workers_.size()); }

private:
    void run(std::stop_token st) {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock lk(m_);
                cv_.wait(lk, [&] { return st.stop_requested() || !queue_.empty(); });
                if (st.stop_requested() && queue_.empty()) return;
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            job();
        }
    }

    std::vector<std::jthread> workers_;
    std::deque<std::function<void()>> queue_;
    std::mutex m_;
    std::condition_variable cv_;
};

}
