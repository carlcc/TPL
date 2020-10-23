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

std::mutex gLoggerMutex;

class MyLogger {
public:
    MyLogger(std::ostream& os, const char* file, int line)
        : os_ { os }
        , file_ { file }
        , line_(line)
    {
        gLoggerMutex.lock();
        os_ << std::this_thread::get_id() << ":time[" << AppTime() << "]: ";
    }
    ~MyLogger()
    {
        os_ << " \t(" << file_ << ":" << line_ << ")" << std::endl;
        gLoggerMutex.unlock();
    }
    template <class T>
    inline MyLogger& operator<<(const T& t)
    {
        os_ << t;
        return *this;
    }

    std::ostream& os_;
    const char* file_;
    int line_;
};

#define LOG MyLogger(std::cout, __FILE__, __LINE__)

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
        LOG << kTask1Name << " started";
        SleepFor(1000);
    }

    void Task2Cb()
    {
        LOG << kTask2Name << " started";
        SleepFor(1000);
    }

    void Task4Cb(const tpl::Task<void>& t1, const tpl::Task<void>& t2)
    {
        LOG << "Task4 started";
    }

    int loopTimes { 0 };
    void Task3Cb(const tpl::Task<void>& t1, const tpl::Task<void>& t2)
    {
        if (t1.Valid()) {
            LOG << "Precede tasks: " << t1.GetName() << ", " << t2.GetName() << " finished. " << kTask3Name << " Start";
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

class Test1 {
public:

    void test1()
    {
        tpl::ParallelTaskScheduler scheduler(8);
        tpl::Task<int> task5 {};
        {
            tpl::Task<int> task(
                []() {
                    SleepFor(1000);
                    LOG << "Task1 ";
                    return 1;
                },
                scheduler);
            tpl::Task<float> task2(
                []() {
                    SleepFor(2000);
                    LOG << "Task2 ";
                    return 3.4f;
                },
                scheduler);
            tpl::Task<void> task3(
                []() {
                    SleepFor(500);
                    LOG << "Task3 ";
                },
                scheduler);

            tpl::Task<int> task4(
                [](const tpl::Task<int>& a, const tpl::Task<float>& b, const tpl::Task<void>& c) -> int {
                    LOG << "Task 4, value: " << a.GetFuture().GetValue() << ", " << b.GetFuture().GetValue();
                    return 2;
                },
                scheduler, task, task2, task3);
            task5 = task4.Then(
                [](const tpl::Task<int>& a) -> int {
                    LOG << "Task 5, value: " << a.GetFuture().GetValue();
                    SleepFor(3000);
                    return 2;
                });
            task.Start();
            task2.Start();
            task3.Start();
        }

        auto result = task5.GetFuture().GetValue();
        LOG << "Result is: " << result;
    }
};

class TestUnwrap {
public:
    tpl::ParallelTaskScheduler scheduler;

    void test()
    {
        // clang-format off
        auto afterInnerTaskReturn = tpl::MakeTaskAndStart(
            [this]() {
                tpl::Task<std::string> wrappedTask = tpl::MakeTask(
                    [this]() -> std::string {
                        SleepFor(1000);
                        return "Hello from inner task";
                    },
                    &scheduler);
                wrappedTask.Start();
                return wrappedTask;
            },
            &scheduler
        ).Unwrap().Then(
            [](const tpl::Task<std::string>& innerTask) {
                LOG << "Then message from inner task is: " << innerTask.GetFuture().GetValue();
                return 100;
            }
        );
        // clang-format on

        LOG << "Waiting for tasks";
        auto& fut = afterInnerTaskReturn.GetFuture();
        fut.Wait();
        LOG << "After inner task return, we get " << fut.GetValue();
    }
};

int main()
{
    SleepFor(0);
    {
        LOG << "===== Start test1";
        Test1 test1;
        test1.test1();
    }
    {
        LOG << "===== Start test2";
        Test2 test2;
        test2.test2(3);
    }
    {
        LOG << "===== Start unwrap test";
        TestUnwrap().test();
    }

    

    LOG << "End";
    return 0;
}
