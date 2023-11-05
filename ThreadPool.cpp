
#include "ThreadPool.h"

#include <chrono>
#include <iostream>

// the constructor just launches some amount of workers
ThreadPool::ThreadPool(size_t threads, bool enable_timer) : stop_(false) {
    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                //std::this_thread::sleep_for(std::chrono::seconds(2));
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this] {
                        return this->stop_ || !this->normal_qtasks_.empty() || !this->priority_qtasks_.empty();
                    });
                    if (this->stop_ && this->normal_qtasks_.empty() && this->priority_qtasks_.empty()) return;
                    if (this->priority_qtasks_.empty()) {
                        task = std::move(this->normal_qtasks_.front()->fun);
                        this->normal_qtasks_.pop();
                    } else {
                        if(!this->priority_qtasks_.front()->fun){
                            std::cout<<"move befor task is nullptr" << std::endl;
                        }
                        task = std::move(this->priority_qtasks_.front()->fun);
                        this->priority_qtasks_.pop();
                    }
                }
                if(!task){
                    std::cout<<"task is nullptr" << std::endl;
                }else{
                    task();
                }
                
            }
        });
    }
    if (enable_timer) {
        isStartTimer_ = enable_timer;
        mWaitTimepoint_ = std::chrono::steady_clock::now() + std::chrono::hours(10000);
        createTimerThread();
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop_ = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers_) worker.join();

    mTimerTaskCV.notify_all();
    if (mTimerThread && mTimerThread->joinable()) {
        mTimerThread->join();
    }
}

// 删除是立刻删除还是在TimerRun 函数中删除，在TimerRun函数中好处是什么？
bool ThreadPool::RemoveTimer(TimerId id) {
    bool isFind = false;
    std::lock_guard<std::mutex> lock{mTimerTaskLock};
    for (auto ite = mTimerTasks.begin(); ite != mTimerTasks.end(); ite++) {
        if ((*ite)->id_ == id) {
            mTimerTasks.erase(ite);
            isFind = true;
            break;
        }
    }
    if (isFind) {
        std::make_heap(mTimerTasks.begin(), mTimerTasks.end(), TimerTaskCompare());
    }

    return isFind;
}

// TODO
void ThreadPool::updateWaitTimer() {
    std::lock_guard<std::mutex> lock{mTimerTaskLock};

    auto ite = mTimerTasks.begin();
    if (ite != mTimerTasks.end()) {
        mWaitTimepoint_ = (*ite)->trigger_timepoint;
    } else {
        mWaitTimepoint_ = std::chrono::steady_clock::now() + std::chrono::hours(10000);
    }
}

void ThreadPool::TimerRun() {
    std::string name = "threadpool_timer";
    pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());

    while (!stop_) {
        updateWaitTimer();  // refresh timer
        std::vector<std::shared_ptr<TimerTask>> tasks;
        {
            std::unique_lock<std::mutex> lck{mTimerTaskLock};
            //std::cout << "===TimerRun-001===" << std::endl;
            mTimerTaskCV.wait_until(lck, mWaitTimepoint_);
            if (stop_) {
                break;
            }
            auto now = std::chrono::steady_clock::now();
            auto ite = mTimerTasks.begin();

            //std::cout << "===TimerRun-002===" << std::endl;
            while (ite != mTimerTasks.end() && now >= (*ite)->trigger_timepoint) {
                tasks.push_back(*ite);
                auto tmp = *ite;
                // 移除堆顶数据到数据结构的最后
                std::pop_heap(mTimerTasks.begin(), mTimerTasks.end(), TimerTaskCompare());
                // 删除最后的元素，就是之前的堆顶
                mTimerTasks.pop_back();

                //std::cout << "===TimerRun-003===" << std::endl;
                if (tmp->one_short) {
                    //std::cout << "===TimerRun-004===" << std::endl;
                    ite = mTimerTasks.begin();  // 如果pop_heap操作完还是堆这样写是可以的
                    continue;
                } else {
                    //std::cout << "===TimerRun-005===" << std::endl;
                    while (tmp->trigger_timepoint < now) {
                        tmp->trigger_timepoint += std::chrono::milliseconds(tmp->cycle);  // update trigger time;
                    }

                    // 重新加入到有序堆中
                    mTimerTasks.push_back(tmp);
                    // 往有序堆中插入，应该比std::make_heap效率要高
                    std::push_heap(mTimerTasks.begin(), mTimerTasks.end(), TimerTaskCompare());
                    ite = mTimerTasks.begin();
                }
            }
        }
        //std::cout << "===TimerRun-006===" << std::endl;
        // do dispatch
        std::unique_lock<std::mutex> lock(queue_mutex);
        for (auto &task : tasks) {
            priority_qtasks_.emplace(task);
            condition.notify_all();
        }
    }
}

void ThreadPool::createTimerThread() {
    if (mTimerThread) {
        return;
    }
    mTimerThread = std::make_shared<std::thread>(std::bind(&ThreadPool::TimerRun, this));
}