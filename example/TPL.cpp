//
// Copyright (c) 2020 Carl Chen. All rights reserved.
//
#include "RefCounted.h"
#include "Scheduler.h"
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>

enum class WaitStatus {
    kTimeout,
    kReady,
};

template <class Ret>
class Future {
public:
    using ValueType = Ret;
    using OnValueAvailable = std::function<void(ValueType)>;

    explicit Future() = default;

    explicit Future(const ValueType& value)
        : value_ { value }
    {
    }

    explicit Future(ValueType&& value)
        : value_ { std::move(value) }
    {
    }

    bool IsReady() const
    {
        std::unique_lock<std::mutex> lck(mutex_);
        return IsReadyInternal();
    }

    void Wait() const
    {
        std::unique_lock<std::mutex> lck(mutex_);
        hasValueCv_.wait(lck, [this]() { return value_.has_value(); });
    }

    WaitStatus WaitFor(size_t millis) const
    {
        bool hasValue;
        {
            std::unique_lock<std::mutex> lck(mutex_);
            hasValue = hasValueCv_.wait_for(lck, std::chrono::milliseconds(millis), [this]() { return value_.has_value(); });
        }
        return hasValue ? WaitStatus::kReady : WaitStatus::kTimeout;
    }

    const ValueType& GetValue() const
    {
        Wait();
        return GetValueInternal();
    }

    void SetValue(const ValueType& v)
    {
        {
            std::unique_lock<std::mutex> lck(mutex_);
            assert(!value_.has_value());
            value_ = v;
        }
        hasValueCv_.notify_all();
        CallAllValueListener();
    }

    void SetValue(ValueType&& v)
    {
        {
            std::unique_lock<std::mutex> lck(mutex_);
            assert(!value_.has_value());
            value_ = std::move(v);
        }
        hasValueCv_.notify_all();
        CallAllValueListener();
    }

    /// NOTE1: The cb will on deleted once it is called
    /// NOTE2: The thread executing the callback is not ensured, and cb may be executed in the caller's thread or in the thread in which the value is set
    void InvokeOnValueAvailable(OnValueAvailable&& cb) const
    {
        assert(cb != nullptr);
        OnValueAvailable callback { nullptr };
        {
            std::unique_lock<std::mutex> lck(mutex_);
            if (IsReadyInternal()) {
                callback = std::move(cb);
            } else {
                onValueAvailable_.push(std::move(cb));
            }
        }
        if (callback != nullptr) {
            callback(GetValueInternal());
        }
    }

private:
    bool IsReadyInternal() const { return value_.has_value(); }

    const ValueType& GetValueInternal() const
    {
        assert(value_.has_value());
        return value_.value();
    }

    void CallAllValueListener() const
    {
        OnValueAvailable cb { nullptr };
        while (true) {
            {
                std::unique_lock<std::mutex> lck(mutex_);
                if (onValueAvailable_.empty()) {
                    break;
                }

                cb = std::move(onValueAvailable_.front());
                onValueAvailable_.pop();
            }
            assert(cb != nullptr);
            cb(GetValueInternal());
        }
    }

private:
    std::optional<ValueType> value_ {};
    mutable std::condition_variable hasValueCv_ {};
    mutable std::queue<OnValueAvailable> onValueAvailable_ {};
    mutable std::mutex mutex_ {};
};

template <>
class Future<void> {
public:
    using OnValueAvailable = std::function<void()>;

    Future() = default;

    explicit Future(bool hasValue)
        : hasValue_ { hasValue }
    {
    }

    bool IsReady() const
    {
        std::unique_lock<std::mutex> lck(mutex_);
        return IsReadyInternal();
    }

    void Wait() const
    {
        std::unique_lock<std::mutex> lck(mutex_);
        hasValueCv_.wait(lck, [this]() { return hasValue_; });
    }

    WaitStatus WaitFor(size_t millis) const
    {
        bool hasValue;
        {
            std::unique_lock<std::mutex> lck(mutex_);
            hasValue = hasValueCv_.wait_for(lck, std::chrono::milliseconds(millis), [this]() { return hasValue_; });
        }
        return hasValue ? WaitStatus::kReady : WaitStatus::kTimeout;
    }

    void GetValue() const
    {
        Wait();
    }

    void SetValue()
    {
        {
            std::unique_lock<std::mutex> lck(mutex_);
            assert(!hasValue_);
            hasValue_ = true;
        }
        hasValueCv_.notify_all();
        CallAllValueListener();
    }

    /// NOTE1: The cb will on deleted once it is called
    /// NOTE2: The thread executing the callback is not ensured, and cb may be executed in the caller's thread or in the thread in which the value is set
    void InvokeOnValueAvailable(OnValueAvailable&& cb) const
    {
        assert(cb != nullptr);
        OnValueAvailable callback { nullptr };
        {
            std::unique_lock<std::mutex> lck(mutex_);
            if (IsReadyInternal()) {
                callback = std::move(cb);
            } else {
                onValueAvailable_.push(std::move(cb));
            }
        }
        if (callback != nullptr) {
            callback();
        }
    }

private:
    bool IsReadyInternal() const { return hasValue_; }

    void CallAllValueListener() const
    {
        OnValueAvailable cb { nullptr };
        while (true) {
            {
                std::unique_lock<std::mutex> lck(mutex_);
                if (onValueAvailable_.empty()) {
                    break;
                }

                cb = std::move(onValueAvailable_.front());
                onValueAvailable_.pop();
            }
            assert(cb != nullptr);
            cb();
        }
    }

private:
    bool hasValue_ { false };
    mutable std::condition_variable hasValueCv_ {};
    mutable std::queue<OnValueAvailable> onValueAvailable_ {};
    mutable std::mutex mutex_ {};
};

template <size_t i, class... Args>
struct NthType {
    using Type = typename std::tuple_element<i, std::tuple<Args...>>::type;
};

template <int n, class Arg, class... Args>
auto&& NthArg(Arg&& arg, Args&&... args)
{
    if constexpr (n == 0) {
        return arg;
    } else {
        return NthArg<n - 1, Args...>(args...);
    }
}

template <class Tuple, typename T, T... S, typename F, class... Args>
constexpr void ForTupleAndArgs(Tuple& tuple, std::integer_sequence<T, S...>, F&& f, Args&&... args)
{
    (void)std::vector<bool> { (static_cast<void>(f(std::get<std::integral_constant<T, S>::value>(tuple), NthArg<std::integral_constant<T, S>::value>(args...))), true)... };
}

template <class T>
class TaskImpl;

template <class T>
class Task {
public:
    using ValueType = T;

    Task() = default;

    template <class Functor, class... ParentTasks>
    Task(Functor&& functor, ITaskScheduler& scheduler, const ParentTasks&... parentTasks);

private:
    explicit Task(TaskImpl<T>* impl);

public:
    void Start();

    auto& GetFuture() const;

    ITaskScheduler* GetScheduler() const;

    const std::string& GetName() const;

    void SetName(const std::string& name);

    void SetName(std::string&& name);

    template <class Functor>
    auto Then(Functor&& functor, ITaskScheduler* scheduler = nullptr);

private:
    RefCountGuard<TaskImpl<T>> impl_ { nullptr };

    template <class U>
    friend class Task;

    template <class ImplType>
    friend Task<typename ImplType::ValueType> MakeTaskFromImpl(ImplType* impl);
};

template <class Functor, class... ParentTasks>
auto MakeTask(Functor&& functor, ITaskScheduler* scheduler, const ParentTasks&... parentTasks)
{
    using ValueType = decltype(functor(parentTasks...));
    using ResultTaskType = Task<ValueType>;
    if (scheduler == nullptr) {
        scheduler = gDefaultTaskScheduler;
        assert(scheduler != nullptr); // "Did you forget to specify a scheduler?"
    }
    return ResultTaskType(functor, *scheduler, parentTasks...);
}

template <class ImplType>
Task<typename ImplType::ValueType> MakeTaskFromImpl(ImplType* impl);

template <class Tuple, class T, T... S, class F>
constexpr auto InvokeFunctionWithTuple(Tuple& tuple, std::integer_sequence<T, S...>, F&& functor)
{
    return functor(MakeTaskFromImpl(std::get<std::integral_constant<T, S>::value>(tuple).Get())...);
}

template <class T>
class TaskImpl final : public RefCounted {
public:
    using ValueType = T;

    template <class Functor>
    TaskImpl(Functor&& functor, ITaskScheduler& scheduler)
        : functor_ { std::forward<Functor>(functor) }
        , scheduler_ { &scheduler }
    {
        LOG << "Construct task " << (void*)this << std::endl;
        static_assert(std::is_invocable_r_v<ValueType, Functor>);
    }

    template <class Functor, class... ParentTasks>
    TaskImpl(Functor&& functor, ITaskScheduler& scheduler, ParentTasks*... parentTasks)
        : functor_ { nullptr }
        , scheduler_ { &scheduler }
    {
        LOG << "Construct task " << (void*)this << std::endl;
        static_assert(std::is_invocable_r_v<ValueType, Functor, Task<typename ParentTasks::ValueType>...>);

        using TupleType = std::tuple<RefCountGuard<ParentTasks>...>;

        struct DependencyContext {
            std::atomic_int pendingDependencyCount { 0 };
            TupleType dependencies {};
        };
        auto spDeps = std::make_shared<DependencyContext>();
        spDeps->pendingDependencyCount = sizeof...(ParentTasks);
        
        // NOTE: Don't pass spDeps, or deps will always keeps it's deps references.
        functor_ = [deps = spDeps.get(), functor = std::forward<Functor>(functor)]() -> ValueType {
            return InvokeFunctionWithTuple(deps->dependencies, std::make_index_sequence<sizeof...(ParentTasks)> {}, functor);
        };

        ForTupleAndArgs(
            spDeps->dependencies,
            std::make_index_sequence<sizeof...(ParentTasks)> {},
            [spDeps, this](auto& tupleEle, auto* parent) {
                using ParentTaskType = std::decay_t<decltype(*parent)>;
                if constexpr (std::is_same_v<typename ParentTaskType::ValueType, void>) {
                    parent->GetFuture().InvokeOnValueAvailable([spDeps, &tupleEle, parent, self = RefCountGuard(this)]() {
                        // Since this callback will be destroyed after is is called,

                        // i.e. self's parent my lose all the references, let's add an reference to keep self's parent valid
                        tupleEle = parent;

                        if (--spDeps->pendingDependencyCount == 0) {
                            // do nothing, just keep spDeps(i.e. it's parents) not being destoryed, spDeps will be released after self becomes ready
                            self->future_.InvokeOnValueAvailable([spDeps](const auto&) {});
                            // i.e. The reference count of self will be released,
                            // but we schedule self before returning
                            // thus the background worker task a reference of self
                            self->Start();
                        }
                    });
                } else {
                    parent->GetFuture().InvokeOnValueAvailable([spDeps, &tupleEle, parent, self = RefCountGuard(this)](const typename ParentTaskType::ValueType& v) {
                        // See above
                        tupleEle = parent;

                        if (--spDeps->pendingDependencyCount == 0) {
                            // see above
                            self->future_.InvokeOnValueAvailable([spDeps](const auto&) {});
                            // See above
                            self->Start();
                        }
                    });
                }
            },
            parentTasks...);
    }

    TaskImpl(const TaskImpl&) = delete;
    TaskImpl(TaskImpl&&) = delete;

    ~TaskImpl() override
    {
        LOG << "Destruct task " << (void*)this << std::endl;
    }

    TaskImpl& operator=(const TaskImpl&) = delete;
    TaskImpl& operator=(TaskImpl&&) = delete;

public:
    void Start()
    {
#if !defined(NDEBUG)
        assert(!isStarted_);
        isStarted_ = true;
#endif
        scheduler_->Schedule([self = RefCountGuard(this)]() mutable {
            if constexpr (std::is_same_v<void, ValueType>) {
                self->functor_();
                self->future_.SetValue();
            } else {
                self->future_.SetValue(self->functor_());
            }
        });
    }

    auto& GetFuture() const { return future_; }

    ITaskScheduler* GetScheduler() const { return scheduler_; }

    const std::string& GetName() const { return name_; }

    void SetName(const std::string& name) { name_ = name; }

    void SetName(std::string&& name) { name_ = std::move(name); }

protected:
    Future<ValueType> future_ {};
    std::function<ValueType()> functor_ { nullptr };
    ITaskScheduler* scheduler_ { nullptr };
    std::string name_ {};
#if !defined(NDEBUG)
    bool isStarted_ { false };
#endif

    template <class U>
    friend class TaksImpl;
};

template <class ImplType>
Task<typename ImplType::ValueType> MakeTaskFromImpl(ImplType* impl)
{
    using ValueType = typename ImplType::ValueType;
    return Task<ValueType>(impl);
}

template <class Functor, class... ParentTasks>
auto MakeTaskImpl(Functor&& functor, ITaskScheduler& scheduler, ParentTasks*... parentTasks)
{
    using ValueType = decltype(functor(MakeTaskFromImpl(parentTasks)...));
    using ResultTaskType = TaskImpl<ValueType>;
    return RefCountGuard<ResultTaskType>(new ResultTaskType(std::forward<Functor>(functor), scheduler, parentTasks...));
}

template <class T>
template <class Functor, class... ParentTasks>
Task<T>::Task(Functor&& functor, ITaskScheduler& scheduler, const ParentTasks&... parentTasks)
    : impl_ { MakeTaskImpl(std::forward<Functor>(functor), scheduler, parentTasks.impl_.Get()...) }
{
}
template <class T>
void Task<T>::Start() { impl_->Start(); }
template <class T>
auto& Task<T>::GetFuture() const { return impl_->GetFuture(); }
template <class T>
const std::string& Task<T>::GetName() const { return impl_->GetName(); }
template <class T>
void Task<T>::SetName(const std::string& name) { impl_->SetName(name); }
template <class T>
void Task<T>::SetName(std::string&& name) { impl_->SetName(std::move(name)); }
template <class T>
Task<T>::Task(TaskImpl<T>* impl)
    : impl_(impl)
{
}
template <class T>
template <class Functor>
auto Task<T>::Then(Functor&& functor, ITaskScheduler* scheduler)
{
    if (scheduler == nullptr) {
        scheduler = GetScheduler();
    }
    return MakeTask(functor, scheduler, *this);
}
template <class T>
ITaskScheduler* Task<T>::GetScheduler() const { return impl_->GetScheduler(); }

void SleepFor(int millis)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

void foo(int, float, double)
{
    std::cout << " ---" << std::endl;
}

int main()
{
    SleepFor(0);
    {
        ParallelTaskScheduler scheduler(1);
        Task<int> task5 {};
        {
            Task<int> task(
                []() {
                    SleepFor(1000);
                    LOG << "Task1 " << std::endl;
                    return 1;
                },
                scheduler);
            Task<float> task2(
                []() {
                    SleepFor(2000);
                    LOG << "Task2 " << std::endl;
                    return 3.4f;
                },
                scheduler);
            Task<void> task3(
                []() {
                    SleepFor(500);
                    LOG << "Task3 " << std::endl;
                },
                scheduler);

            Task<int> task4(
                [](const Task<int>& a, const Task<float>& b, const Task<void>& c) -> int {
                    LOG << "Task 4, value: " << a.GetFuture().GetValue() << ", " << b.GetFuture().GetValue() << std::endl;
                    return 2;
                },
                scheduler, task, task2, task3);
            task5 = task4.Then(
                [](const Task<int>& a) -> int {
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