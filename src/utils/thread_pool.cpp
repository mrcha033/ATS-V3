#include "thread_pool.hpp"

namespace ats {

ThreadPool::ThreadPool(size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) {
            num_threads = 4; // fallback
        }
    }

    threads_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        threads_.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::worker_thread() {
    while (!stop_) {
        Task task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            
            if (stop_ && tasks_.empty()) {
                return;
            }
            
            if (!tasks_.empty()) {
                task = std::move(const_cast<Task&>(tasks_.top()));
                tasks_.pop();
                ++active_tasks_;
            }
        }
        
        if (task.function) {
            task.function();
            --active_tasks_;
            finished_condition_.notify_all();
        }
    }
}

void ThreadPool::wait_for_all() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    finished_condition_.wait(lock, [this] { 
        return tasks_.empty() && active_tasks_ == 0; 
    });
}

size_t ThreadPool::pending_tasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    
    condition_.notify_all();
    
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    threads_.clear();
}

} // namespace ats