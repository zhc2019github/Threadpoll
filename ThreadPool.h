#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include <iostream>

using TimerId = uint64_t;

class BaseTask {
public:
    BaseTask(){};
    std::function<void()> fun{nullptr};

private:
};

class TimerTask : public BaseTask {
public:
    TimerTask() {
        static std::atomic_uint64_t sId{0};
        id_ = sId.fetch_add(1);
    }

    TimerId id_;

    std::chrono::steady_clock::time_point trigger_timepoint;
    bool one_short{false};
    bool is_remove{false};
    uint32_t cycle{10};  // ms

private:
};

class WorkTask : public BaseTask {
public:
    WorkTask() : BaseTask(){};

private:
};

class ThreadPool {
public:
    ThreadPool(size_t threads, bool enable_timer=true);
    ~ThreadPool();

    // add new work item to the pool
    template <class F, class... Args>
    auto AddTask(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> res = task->get_future();

        std::shared_ptr<WorkTask> work_task = std::shared_ptr<WorkTask>(new WorkTask);
        work_task->fun = [task]() { (*task)(); };
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // don't allow AddTask after stopping the pool
            if (stop_) throw std::runtime_error("AddTask on stopped ThreadPool");

            normal_qtasks_.emplace(work_task);
            
        }
        condition.notify_one();
        return res;
    };

    template <class F, class... Args>
    TimerId AddTimer(bool one_short, uint32_t cycle, F&& f, Args&&... args) {
        if (isStartTimer_ && !stop_) {
            std::shared_ptr<TimerTask> timer_task = std::shared_ptr<TimerTask>(new TimerTask);
            timer_task->one_short = one_short;
            timer_task->cycle = cycle;
            timer_task->fun = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            timer_task->trigger_timepoint = std::chrono::steady_clock::now() + std::chrono::milliseconds(cycle);

            std::lock_guard<std::mutex> lck{mTimerTaskLock};
            mTimerTasks.emplace_back(timer_task);
            //生成小顶堆
            std::make_heap(mTimerTasks.begin(), mTimerTasks.end(), TimerTaskCompare());
            //std::push_heap(mTimerTasks.begin(), mTimerTasks.end(), TimerTaskCompare()); 是否可以替代上一行

            mTimerTaskCV.notify_all();
            return timer_task->id_;
        }
        return std::numeric_limits<uint64_t>::max();
    };

    bool RemoveTimer(TimerId id);

private:
    // need to keep track of threads so we can join them
    std::vector<std::thread> workers_;
    // the task queue
    std::queue<std::shared_ptr<BaseTask>> normal_qtasks_;
    std::queue<std::shared_ptr<BaseTask>> priority_qtasks_;


    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop_;

    bool isStartTimer_{false};
    std::mutex mTimerTaskLock;
    std::condition_variable mTimerTaskCV;
    std::vector<std::shared_ptr<TimerTask>> mTimerTasks;
    std::shared_ptr<std::thread> mTimerThread;
    std::chrono::steady_clock::time_point mWaitTimepoint_;
    struct TimerTaskCompare {
        bool operator()(const std::shared_ptr<TimerTask>& a, const std::shared_ptr<TimerTask>& b) {
            return a->trigger_timepoint > b->trigger_timepoint;
        }
    };
    void TimerRun();
    void updateWaitTimer();
    void createTimerThread();
};

#endif
