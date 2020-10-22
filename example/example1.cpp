//
// Copyright (c) 2020 Carl Chen. All rights reserved.
//
#include <TPL/TPL.h>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

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
void SleepFor(int millis)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

static const char* kTask1Name = "Request1(1)";
static const char* kTask2Name = "Request2(2)";
static const char* kTask3Name = "Download(3)";

class Test2 {
public:
    class CustomScheduler : public tpl::ITaskScheduler {
    public:
        void Schedule(const std::function<void()>& functor) override
        {
            {
                std::unique_lock<std::mutex> lck(queueMutex_);
                taskQueue_.push(functor);
            }
            queueCv_.notify_one();
        }

        void Stop()
        {
            {
                std::unique_lock<std::mutex> lck(queueMutex_);
                isRunning_ = false;
            }
            queueCv_.notify_all();
        }

        void Loop()
        {
            isRunning_ = true;
            while (true) {
                std::function<void()> functor { nullptr };
                {
                    std::unique_lock<std::mutex> lck(queueMutex_);
                    queueCv_.wait(lck, [this]() {
                        return !taskQueue_.empty() || !isRunning_;
                    });
                    if (!isRunning_ && taskQueue_.empty()) {
                        break;
                    }
                    functor = taskQueue_.front();
                    taskQueue_.pop();
                }
                assert(functor != nullptr);
                functor();
            }
        }

        std::mutex queueMutex_;
        std::condition_variable queueCv_;
        std::queue<std::function<void()>> taskQueue_;
        bool isRunning_ { false };
    };

    tpl::ParallelTaskScheduler scheduler { std::thread::hardware_concurrency() };
    CustomScheduler scheduler2;

    void Task1Cb()
    {
        LOG << kTask1Name << " started" << std::endl;
        SleepFor(1000);
    }

    void Task2Cb()
    {
        LOG << kTask2Name << " started" << std::endl;
        SleepFor(1000);
    }

    void Task4Cb(const tpl::Task<void>& t1, const tpl::Task<void>& t2)
    {
        LOG << "Task4 started" << std::endl;
    }

    int loopTimes { 0 };
    void Task3Cb(const tpl::Task<void>& t1, const tpl::Task<void>& t2)
    {
        if (t1.Valid()) {
            LOG << "Precede tasks: " << t1.GetName() << ", " << t2.GetName() << " finished. " << kTask3Name << " Start" << std::endl;
        }
        if (loopTimes-- == 0) {
            scheduler2.Stop();
            return;
        }
        auto task1 = tpl::MakeTask([this] { Task1Cb(); }, &scheduler);
        auto task2 = tpl::MakeTask([this] { Task2Cb(); }, &scheduler);
        auto task3 = tpl::MakeTask([this](const auto& t1, const auto& t2) { Task3Cb(t1, t2); }, &scheduler, task1, task2);
        auto task4 = tpl::MakeTask([this](const auto& t1, const auto& t2) { Task4Cb(t1, t2); }, &scheduler2, task1, task2);
        task1.SetName(kTask1Name);
        task2.SetName(kTask2Name);
        task1.Start();
        task2.Start();
    }

    void test2(int n = 3)
    {
        loopTimes = n;
        Task3Cb({}, {});

        scheduler2.Loop();
    }
};

int main()
{
    SleepFor(0);
    Test2 test2;
    test2.test2(3);

    {
        tpl::ParallelTaskScheduler scheduler(8);
        tpl::Task<int> task5 {};
        {
            tpl::Task<int> task(
                []() {
                    SleepFor(1000);
                    LOG << "Task1 " << std::endl;
                    return 1;
                },
                scheduler);
            tpl::Task<float> task2(
                []() {
                    SleepFor(2000);
                    LOG << "Task2 " << std::endl;
                    return 3.4f;
                },
                scheduler);
            tpl::Task<void> task3(
                []() {
                    SleepFor(500);
                    LOG << "Task3 " << std::endl;
                },
                scheduler);

            tpl::Task<int> task4(
                [](const tpl::Task<int>& a, const tpl::Task<float>& b, const tpl::Task<void>& c) -> int {
                    LOG << "Task 4, value: " << a.GetFuture().GetValue() << ", " << b.GetFuture().GetValue() << std::endl;
                    return 2;
                },
                scheduler, task, task2, task3);
            task5 = task4.Then(
                [](const tpl::Task<int>& a) -> int {
                    LOG << "Task 5, value: " << a.GetFuture().GetValue() << std::endl;
                    SleepFor(3000);
                    return 2;
                });
            task.Start();
            task2.Start();
            task3.Start();
        }

        auto result = task5.GetFuture().GetValue();
        LOG << "Result is: " << result << std::endl;
    }

    LOG << "End" << std::endl;
    return 0;
}
