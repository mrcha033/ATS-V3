#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>

namespace ats {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Submit a task to the thread pool
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>>;

    // Submit a task with priority (higher number = higher priority)
    template<typename F, typename... Args>
    auto submit_priority(int priority, F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>>;

    // Wait for all tasks to complete
    void wait_for_all();

    // Get number of active threads
    size_t thread_count() const { return threads_.size(); }

    // Get number of pending tasks
    size_t pending_tasks() const;

    // Check if the pool is running
    bool is_running() const { return !stop_; }

    // Shutdown the pool
    void shutdown();

private:
    struct Task {
        std::function<void()> function;
        int priority;
        
        bool operator<(const Task& other) const {
            return priority < other.priority; // Higher priority first
        }
    };

    std::vector<std::thread> threads_;
    std::priority_queue<Task> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
    std::condition_variable finished_condition_;

    void worker_thread();
};

template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>> {
    return submit_priority(0, std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto ThreadPool::submit_priority(int priority, F&& f, Args&&... args) -> std::future<typename std::invoke_result_t<F, Args...>> {
    using return_type = typename std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        if (stop_) {
            throw std::runtime_error("Cannot submit task to stopped ThreadPool");
        }

        tasks_.emplace(Task{[task]() { (*task)(); }, priority});
    }

    condition_.notify_one();
    return result;
}

} // namespace ats