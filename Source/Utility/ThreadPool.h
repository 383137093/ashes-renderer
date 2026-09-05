#pragma once

#include <mutex>
#include <queue>
#include <atomic>
#include <future>
#include <memory>
#include <thread>
#include <vector>
#include <cstdlib>
#include <utility>
#include <algorithm>
#include <functional>
#include <type_traits>
#include <condition_variable>

namespace Ashes {

class ThreadPool 
{
public:

    struct WithBatchIDTag {};
    static inline const WithBatchIDTag kWithBatchID;

    explicit ThreadPool(int num_threads)
    {
        for (int i = 0; i < num_threads; ++i)
            threads_.emplace_back(&ThreadPool::ThreadFunc, this);
    }

    ~ThreadPool()
    {
        Stop();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator = (const ThreadPool&) = delete;
    ThreadPool& operator = (ThreadPool&&) = delete;

    template <typename F, typename... Args,
        typename R=std::invoke_result_t<F, Args...>>
    std::future<R> Enqueue(F&& f, Args&&... args)
    {
        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::lock_guard<std::mutex> locker(mutex_);
        tasks_.push([task]() { (*task)(); });
        condition_.notify_one();
        return task->get_future();
    }

    static std::pair<int, int> ComputeBatchRange(
        int num_tasks, int num_batches, int batch_idx)
    {
        std::div_t d = std::div(num_tasks, num_batches);
        int first = batch_idx * d.quot + (std::min)(batch_idx, d.rem);
        int last = first + d.quot + (batch_idx < d.rem ? 1 : 0);
        return {first, last};
    }

    template <typename F, typename... Args>
    std::future<void> DispatchAsync(
        int num_tasks, int num_batches, F&& f, Args&&... args)
    {
        auto context = DispatchImpl(num_tasks, num_batches, 1, 
            BuildBatchFunc(std::forward<F>(f), std::forward<Args>(args)...));
        return context->finished_promise.get_future();
    }

    template <typename F, typename... Args>
    void Dispatch(int num_tasks, int num_batches, F&& f, Args&&... args)
    {
        if (num_batches > 1)
        {
            auto [first, last] = ComputeBatchRange(num_tasks, num_batches, 0);
            auto context = DispatchImpl(num_tasks, num_batches, 1, 
                BuildBatchFunc(std::forward<F>(f), std::forward<Args>(args)...));
            context->batch_func(0, first, last);
            context->finished_promise.get_future().wait();
        }
        else if (num_batches == 1)
        {
            std::invoke(BuildBatchFunc(std::forward<F>(f), 
                std::forward<Args>(args)...), 0, 0, num_tasks);
        }
    }

    void Stop()
    {
        stopped_ = true;
        condition_.notify_all();
        std::for_each(threads_.begin(), threads_.end(),
            std::mem_fn(&std::thread::join));
        threads_.clear();
    }

private:

    using BatchFuncType = std::function<void(
        int batch_id, int first_task, int last_task)>;

    struct TaskBatchContext
    {
        BatchFuncType      batch_func;
        std::atomic_int    unfinished_batch_count;
        std::promise<void> finished_promise;
    };

    void ThreadFunc()
    {
        for (std::function<void()> func; true; func())
        {
            std::unique_lock<std::mutex> locker(mutex_);
            condition_.wait(locker, [this] { return stopped_ || !tasks_.empty(); });
            if (stopped_ && tasks_.empty()) { break; }
            tasks_.front().swap(func);
            tasks_.pop();
        }
    }

    template <typename F, typename... Args>
    static BatchFuncType BuildBatchFunc(F&& f, Args&&... args)
    {
        return std::bind(std::forward<F>(f), std::forward<Args>(args)...,
            std::placeholders::_2, std::placeholders::_3);
    }

    template <typename F, typename... Args>
    static BatchFuncType BuildBatchFunc(F&& f, WithBatchIDTag, Args&&... args)
    {
        return std::bind(std::forward<F>(f), std::forward<Args>(args)...,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    }

    std::shared_ptr<TaskBatchContext> DispatchImpl(int num_tasks,
        int num_batches, int start_batch, BatchFuncType&& batch_func)
    {
        auto context = std::make_shared<TaskBatchContext>();
        context->unfinished_batch_count = num_batches - start_batch;
        context->batch_func = std::move(batch_func);
        
        std::lock_guard<std::mutex> locker(mutex_);
        for (int i = start_batch; i < num_batches; ++i)
        {
            auto [first, last] = ComputeBatchRange(num_tasks, num_batches, i);
            tasks_.emplace([context, i, first, last]() {
                context->batch_func(i, first, last);
                if (context->unfinished_batch_count.fetch_sub(1) == 1)
                    context->finished_promise.set_value(); });
        }
        
        condition_.notify_all();
        return context;
    }

private:

    std::vector<std::thread>          threads_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mutex_;
    std::condition_variable           condition_;
    std::atomic_bool                  stopped_ = false;
};

}