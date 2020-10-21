//
// Copyright (c) 2020 Carl Chen. All rights reserved.
//

#pragma once

#include <atomic>
#include <cassert>

struct RefCounted {
    std::atomic_int refCount { 0 };
    virtual ~RefCounted() = default;

    void AddRef()
    {
        assert(refCount >= 0);
        ++refCount;
    }

    void Release()
    {
        assert(refCount > 0);
        if (--refCount == 0) {
            Delete(this);
        }
    }

    static void Delete(RefCounted* obj)
    {
        delete obj;
    }
};

template <class Type>
struct RefCountGuard {
    using PointerType = Type*;

    RefCountGuard()
        : ref_ { nullptr }
    {
    }

    RefCountGuard(Type* ref)
        : ref_ { ref }
    {
        AddRef();
    }

    RefCountGuard(decltype(nullptr))
        : ref_(nullptr)
    {
    }

    ~RefCountGuard()
    {
        Release();
    }

    RefCountGuard(const RefCountGuard& ref)
    {
        ref_ = ref.ref_;
        AddRef();
    }

    RefCountGuard(RefCountGuard&& ref) noexcept
        : ref_(ref.ref_)
    {
        ref.ref_ = nullptr;
    }

    RefCountGuard& operator=(Type* ref)
    {
        if (ref_ != ref) {
            Release();
            ref_ = ref;
            AddRef();
        }
        return *this;
    }

    RefCountGuard& operator=(decltype(nullptr))
    {
        if (ref_ != nullptr) {
            ref_->Release();
            ref_ = nullptr;
        }
        return *this;
    }

    RefCountGuard& operator=(const RefCountGuard& ref)
    {
        if (ref_ != ref.ref_) {
            Release();
            ref_ = ref.ref_;
            AddRef();
        }
        return *this;
    }

    RefCountGuard& operator=(RefCountGuard&& ref) noexcept
    {
        std::swap(ref.ref_, ref_);
        return *this;
    }

    Type* operator->() const { return ref_; }

    Type& operator*() const { return *ref_; }

    Type* Get() const { return ref_; }

    operator Type*() const noexcept { return ref_; }

private:
    void Release()
    {
        if (ref_ != nullptr) {
            ref_->Release();
        }
    }

    void AddRef()
    {
        if (ref_ != nullptr) {
            ref_->AddRef();
        }
    }

private:
    Type* ref_ { nullptr };
};