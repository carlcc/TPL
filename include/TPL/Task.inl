//
// Copyright (c) 2020 Carl Chen. All rights reserved.
//

#pragma once

#include "Task.h"

namespace tpl {

//====== TaskImpl

namespace detail {

    template <int n, class Arg, class... Args>
    inline auto&& NthArg(Arg&& arg, Args&&... args)
    {
        if constexpr (n == 0) {
            return arg;
        } else {
            return NthArg<n - 1, Args...>(std::forward<Args>(args)...);
        }
    }

    template <class Tuple, typename T, T... S, typename F, class... Args>
    inline constexpr void ForTupleAndArgs(Tuple& tuple, std::integer_sequence<T, S...>, F&& f, Args&&... args)
    {
        (void)std::vector<bool> { (static_cast<void>(f(std::get<std::integral_constant<T, S>::value>(tuple), NthArg<std::integral_constant<T, S>::value>(args...))), true)... };
    }

    template <class Tuple, class T, T... S, class F>
    inline constexpr auto InvokeFunctionWithTuple(Tuple& tuple, std::integer_sequence<T, S...>, F&& functor)
    {
        return functor(MakeTaskFromImpl(std::get<std::integral_constant<T, S>::value>(tuple).Get())...);
    }
}

namespace internal {

    template <class T>
    class TaskImpl final : public RefCounted {
    public:
        using ValueType = T;

        template <class Functor>
        TaskImpl(Functor&& functor, ITaskScheduler& scheduler);

        template <class Functor, class... ParentTasks>
        TaskImpl(Functor&& functor, ITaskScheduler& scheduler, ParentTasks*... parentTasks);

        TaskImpl(const TaskImpl&) = delete;
        TaskImpl(TaskImpl&&) = delete;

        ~TaskImpl() override;

        TaskImpl& operator=(const TaskImpl&) = delete;
        TaskImpl& operator=(TaskImpl&&) = delete;

    public:
        void Start();

        auto& GetFuture() const { return future_; }

        ITaskScheduler* GetScheduler() const { return scheduler_; }

        const std::string& GetName() const { return name_; }

        void SetName(const std::string& name) { name_ = name; }

        void SetName(std::string&& name) { name_ = std::move(name); }

#if !defined(NDEBUG)
    private:
        void MarkAsStarted()
        {
            assert(!isStarted_);
            isStarted_ = true;
        }

        friend class Task<ValueType>;
#endif

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

    template <class Functor, class... ParentTasks>
    inline auto MakeTaskImpl(Functor&& functor, ITaskScheduler& scheduler, ParentTasks*... parentTasks);

    template <class T>
    template <class Functor>
    inline TaskImpl<T>::TaskImpl(Functor&& functor, ITaskScheduler& scheduler)
        : functor_ { std::forward<Functor>(functor) }
        , scheduler_ { &scheduler }
    {
        static_assert(std::is_invocable_r_v<ValueType, Functor>);
    }

    template <class T>
    template <class Functor, class... ParentTasks>
    inline TaskImpl<T>::TaskImpl(Functor&& functor, ITaskScheduler& scheduler, ParentTasks*... parentTasks)
        : functor_ { nullptr }
        , scheduler_ { &scheduler }
    {
        static_assert(std::is_invocable_r_v<ValueType, Functor, Task<typename ParentTasks::ValueType>...>);
        constexpr auto kNumberOfParents = sizeof...(ParentTasks);

        using TupleType = std::tuple<RefCntAutoPtr<ParentTasks>...>;

        struct DependencyContext {
            std::atomic_int pendingDependencyCount { 0 };
            TupleType dependencies {};
        };

        auto spDeps = std::make_shared<DependencyContext>();
        spDeps->pendingDependencyCount = kNumberOfParents;

        // NOTE: Don't pass spDeps, or deps will always keeps it's deps references.
        functor_ = [deps = spDeps.get(), functor = std::forward<Functor>(functor)]() -> ValueType {
            return detail::InvokeFunctionWithTuple(deps->dependencies, std::make_index_sequence<sizeof...(ParentTasks)> {}, functor);
        };

        detail::ForTupleAndArgs(
            spDeps->dependencies,
            std::make_index_sequence<kNumberOfParents> {},
            [spDeps, this](auto& tupleEle, auto* parent) {
                using ParentTaskType = std::decay_t<decltype(*parent)>;
                if constexpr (std::is_same_v<typename ParentTaskType::ValueType, void>) {
                    parent->GetFuture().InvokeOnValueAvailable([spDeps, &tupleEle, parent, self = RefCntAutoPtr(this)]() {
                        // Since this callback will be destroyed after is is called,

                        // i.e. self's parent my lose all the references, let's add an reference to keep self's parent valid
                        tupleEle = parent;

                        if (--spDeps->pendingDependencyCount == 0) {
                            // do nothing, just keep spDeps(i.e. it's parents) not being destoryed, spDeps will be released after self becomes ready
                            if constexpr (std::is_same_v<ValueType, void>) {
                                self->future_.InvokeOnValueAvailable([spDeps]() {});
                            } else {
                                self->future_.InvokeOnValueAvailable([spDeps](const auto&) {});
                            }
                            // i.e. The reference count of self will be released,
                            // but we schedule self before returning
                            // thus the background worker task a reference of self
                            self->Start();
                        }
                    });
                } else {
                    parent->GetFuture().InvokeOnValueAvailable([spDeps, &tupleEle, parent, self = RefCntAutoPtr(this)](const typename ParentTaskType::ValueType& v) {
                        // See above
                        tupleEle = parent;

                        if (--spDeps->pendingDependencyCount == 0) {
                            // see above
                            if constexpr (std::is_same_v<ValueType, void>) {
                                self->future_.InvokeOnValueAvailable([spDeps]() {});
                            } else {
                                self->future_.InvokeOnValueAvailable([spDeps](const auto&) {});
                            }
                            // See above
                            self->Start();
                        }
                    });
                }
            },
            parentTasks...);
    }

    template <class T>
    inline TaskImpl<T>::~TaskImpl()
    {
    }

    template <class T>
    inline void TaskImpl<T>::Start()
    {
#if !defined(NDEBUG)
        MarkAsStarted();
#endif
        scheduler_->Schedule([self = RefCntAutoPtr(this)]() mutable {
            if constexpr (std::is_same_v<void, ValueType>) {
                self->functor_();
                self->future_.SetValue();
            } else {
                self->future_.SetValue(self->functor_());
            }
        });
    }

    template <class Functor, class... ParentTasks>
    inline auto MakeTaskImpl(Functor&& functor, ITaskScheduler& scheduler, ParentTasks*... parentTasks)
    {
        using ValueType = decltype(functor(MakeTaskFromImpl(parentTasks)...));
        using ResultTaskType = TaskImpl<ValueType>;
        return RefCntAutoPtr<ResultTaskType>(new ResultTaskType(std::forward<Functor>(functor), scheduler, parentTasks...));
    }

}

//========== Task
template <class T>
template <class Functor, class... ParentTasks>
inline Task<T>::Task(Functor&& functor, ITaskScheduler& scheduler, const ParentTasks&... parentTasks)
    : impl_ { nullptr }
{
    // The `Functor` is a value
    if constexpr (std::is_same_v<std::decay_t<Functor>, ValueType>) {
        static_assert(sizeof...(ParentTasks) == 0, "Make task from value should have no parent task");
        impl_ = internal::MakeTaskImpl(std::function<ValueType()>(nullptr), scheduler);
#if !defined(NDEBUG)
        MarkAsStarted();
#endif
        const_cast<Future<ValueType>&>(GetFuture()).SetValue(std::forward<Functor>(functor));
    } else {
        impl_ = internal::MakeTaskImpl(std::forward<Functor>(functor), scheduler, parentTasks.impl_.Get()...);
    }
}

template <class T>
inline bool Task<T>::Valid() const
{
    return impl_ != nullptr;
}

template <class T>
inline void Task<T>::Start() { impl_->Start(); }

template <class T>
inline auto& Task<T>::GetFuture() const { return impl_->GetFuture(); }

template <class T>
inline const std::string& Task<T>::GetName() const { return impl_->GetName(); }

template <class T>
inline void Task<T>::SetName(const std::string& name) { impl_->SetName(name); }

template <class T>
inline void Task<T>::SetName(std::string&& name) { impl_->SetName(std::move(name)); }

template <class T>
inline Task<T>::Task(internal::TaskImpl<T>* impl)
    : impl_(impl)
{
}

template <class T>
template <class Functor>
inline auto Task<T>::Then(Functor&& functor, ITaskScheduler* scheduler)
{
    if (scheduler == nullptr) {
        scheduler = GetScheduler();
    }
    return MakeTask(functor, scheduler, *this);
}

template <class T>
inline auto Task<T>::Unwrap(ITaskScheduler* scheduler) -> ValueType
{
    using UnwrappedValueType = typename ValueType::ValueType;
    using UnwrappedTask = Task<UnwrappedValueType>;
    static_assert(std::is_same_v<ValueType, UnwrappedTask>, "Unwrap function only available in embeded Task classes");
    if (scheduler == nullptr) {
        scheduler = gDefaultTaskScheduler;
        assert(scheduler != nullptr); // "Did you forget to specify a scheduler?"
    }
    using CallbackType = std::function<UnwrappedValueType()>;
    UnwrappedTask proxyTask(CallbackType(nullptr), *scheduler); // This task will actually not be executed, so just keep the callback null
#if !defined(NDEBUG)
    proxyTask.MarkAsStarted();
#endif
    // Here, this.future_ can't be a future of void
    GetFuture().InvokeOnValueAvailable([proxyTask](const UnwrappedTask& innerTask) {
        innerTask.GetFuture().InvokeOnValueAvailable([proxyTask](const UnwrappedValueType& value) {
            const_cast<Future<UnwrappedValueType>&>(proxyTask.GetFuture()).SetValue(value);
        });
    });
    return proxyTask;
}

template <class T>
inline auto Task<T>::Unwrap() -> typename ValueType
{
    return Unwrap(GetScheduler());
}

#if !defined(NDEBUG)
template <class T>
inline void tpl::Task<T>::MarkAsStarted()
{
    impl_->MarkAsStarted();
}
#endif

template <class T>
inline ITaskScheduler* Task<T>::GetScheduler() const
{
    return impl_->GetScheduler();
}

template <class Functor, class... ParentTasks>
inline auto MakeTask(Functor&& functor, ITaskScheduler* scheduler, const ParentTasks&... parentTasks)
{
    using ValueType = decltype(functor(parentTasks...));
    using ResultTaskType = Task<ValueType>;
    if (scheduler == nullptr) {
        scheduler = gDefaultTaskScheduler;
        assert(scheduler != nullptr); // "Did you forget to specify a scheduler?"
    }
    return ResultTaskType(functor, *scheduler, parentTasks...);
}

template <class Functor>
auto MakeTaskAndStart(Functor&& functor, ITaskScheduler* scheduler)
{
    if (scheduler == nullptr) {
        scheduler = gDefaultTaskScheduler;
        assert(scheduler != nullptr); // "Did you forget to specify a scheduler?"
    }
    auto task = MakeTask(std::forward<Functor>(functor), scheduler);
    task.Start();
    return task;
}

template <class ValueType>
auto MakeTaskFromValue(ValueType&& value, ITaskScheduler* scheduler)
{
    Task<std::decay_t<ValueType>> task(std::forward<ValueType>(value), *scheduler);
    return task;
}

template <class ImplType>
inline Task<typename ImplType::ValueType> MakeTaskFromImpl(ImplType* impl)
{
    using ValueType = typename ImplType::ValueType;
    return Task<ValueType>(impl);
}

}