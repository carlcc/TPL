//
// Copyright (c) 2020 Carl Chen. All rights reserved.
//

#pragma once

#include <cassert>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>

namespace tpl {

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

}