//
// Copyright (c) 2020 Carl Chen. All rights reserved.
//

#pragma once

#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>


int64_t CurrentTime()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int64_t AppTime()
{
    static int64_t StartTime = CurrentTime();
    return CurrentTime() - StartTime;
}
#define LOG std::cout << std::this_thread::get_id() << ":" << __FILE__ << ":" << __LINE__ << ":time[" << AppTime() << "]: "

class ITaskScheduler {
public:
    virtual ~ITaskScheduler() = default;

    virtual void Schedule(const std::function<void()>& functor) = 0;
};

class ParallelTaskScheduler final : public ITaskScheduler {
public:
    explicit ParallelTaskScheduler(size_t numThreads)
    {
        assert(numThreads > 0); // Ensure the thread number > 0
        workerThreads_.resize(numThreads);

        isRunning_ = true;
        for (auto& th : workerThreads_) {
            th = std::thread([this]() {
                WorkerThreadRoutine();
            });
        }
    }

    ParallelTaskScheduler(ParallelTaskScheduler&&) = delete;
    ParallelTaskScheduler(const ParallelTaskScheduler&) = delete;
    ParallelTaskScheduler& operator=(ParallelTaskScheduler&&) = delete;
    ParallelTaskScheduler& operator=(const ParallelTaskScheduler&) = delete;

    ~ParallelTaskScheduler() final
    {
        {
            std::unique_lock<std::mutex> lck(queueMutex_);
            isRunning_ = false;
        }
        queueCv_.notify_all();
        for (auto& th : workerThreads_) {
            if (th.joinable()) {
                th.join();
            }
        }
    }

    void Schedule(const std::function<void()>& functor) final
    {
        {
            std::unique_lock<std::mutex> lck(queueMutex_);
            ++taskCount_;
            taskQueue_.push(functor);
        }
        queueCv_.notify_one();
    }

private:
    void WorkerThreadRoutine()
    {
        while (true) {
            std::function<void()> functor { nullptr };
            {
                std::unique_lock<std::mutex> lck(queueMutex_);
                queueCv_.wait(lck, [this]() {
                    return taskCount_ > 0 || !isRunning_;
                });
                if (!isRunning_ && taskCount_ == 0) {
                    break;
                }
                functor = taskQueue_.front();
                --taskCount_;
                taskQueue_.pop();
            }
            assert(functor != nullptr);
            functor();
        }
    }

private:
    std::vector<std::thread> workerThreads_ {};

    // The concurrent queue
    std::queue<std::function<void()>> taskQueue_ {};
    size_t taskCount_ { 0 };
    std::mutex queueMutex_ {};
    std::condition_variable queueCv_ {};

    bool isRunning_ { false };
};

ITaskScheduler* gDefaultTaskScheduler { nullptr };

void SetDefaultTaskScheduler(ITaskScheduler* scheduler)
{
    gDefaultTaskScheduler = scheduler;
}

ITaskScheduler* GetDefaultTaskScheduler()
{
    return gDefaultTaskScheduler;
}