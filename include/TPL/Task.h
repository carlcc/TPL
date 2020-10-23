//
// Copyright (c) 2020 Carl Chen. All rights reserved.
//

#pragma once

#include "TPL/Future.h"
#include "TPL/RefCntAutoPtr.h"
#include "TPL/RefCounted.h"
#include <type_traits>

// TODO: Introduce concept if compiled with C++20

namespace tpl {

namespace internal {
    template <class T>
    class TaskImpl;

}

template <class T>
class Task {
public:
    using ValueType = T;

    Task() = default;

    Task(const Task&) = default;

    Task(Task&&) noexcept = default;

    template <class Functor, class... ParentTasks>
    Task(Functor&& functor, ITaskScheduler& scheduler, const ParentTasks&... parentTasks);

    Task& operator=(const Task&) = default;

    Task& operator=(Task&&) noexcept = default;

private:
    explicit Task(internal::TaskImpl<T>* impl);

public:
    bool Valid() const;

    void Start();

    auto& GetFuture() const;

    ITaskScheduler* GetScheduler() const;

    const std::string& GetName() const;

    void SetName(const std::string& name);

    void SetName(std::string&& name);

    template <class Functor>
    auto Then(Functor&& functor, ITaskScheduler* scheduler = nullptr);

    /// Unwrap function create a proxy task that represents asynchronous operation of Task<Task<T>>.
    /// i.e. A Task<Task<T>>::Unrap returns a Task<T> object
    auto Unwrap(ITaskScheduler* scheduler) -> typename ValueType;

#if !defined(NDEBUG)
private:
    void MarkAsStarted();
#endif

private:
    RefCntAutoPtr<internal::TaskImpl<T>> impl_ { nullptr };

    template <class U>
    friend class Task;

    template <class ImplType>
    friend Task<typename ImplType::ValueType> MakeTaskFromImpl(ImplType* impl);
};

template <class Functor, class... ParentTasks>
auto MakeTask(Functor&& functor, ITaskScheduler* scheduler, const ParentTasks&... parentTasks);

}

#include "Task.inl"