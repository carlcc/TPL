//
// Copyright (c) 2020 Carl Chen. All rights reserved.
//

#pragma once

namespace tpl {

template <class Type>
struct RefCntAutoPtr {
    using PointerType = Type*;

    RefCntAutoPtr()
        : ref_ { nullptr }
    {
    }

    RefCntAutoPtr(Type* ref)
        : ref_ { ref }
    {
        AddRef();
    }

    RefCntAutoPtr(decltype(nullptr))
        : ref_(nullptr)
    {
    }

    ~RefCntAutoPtr()
    {
        Release();
    }

    RefCntAutoPtr(const RefCntAutoPtr& ref)
    {
        ref_ = ref.ref_;
        AddRef();
    }

    RefCntAutoPtr(RefCntAutoPtr&& ref) noexcept
        : ref_(ref.ref_)
    {
        ref.ref_ = nullptr;
    }

    RefCntAutoPtr& operator=(Type* ref)
    {
        if (ref_ != ref) {
            Release();
            ref_ = ref;
            AddRef();
        }
        return *this;
    }

    RefCntAutoPtr& operator=(decltype(nullptr))
    {
        if (ref_ != nullptr) {
            ref_->Release();
            ref_ = nullptr;
        }
        return *this;
    }

    RefCntAutoPtr& operator=(const RefCntAutoPtr& ref)
    {
        if (ref_ != ref.ref_) {
            Release();
            ref_ = ref.ref_;
            AddRef();
        }
        return *this;
    }

    RefCntAutoPtr& operator=(RefCntAutoPtr&& ref) noexcept
    {
        if (ref.ref_ != ref_) {
            Release();
            ref_ = ref.ref_;
            ref.ref_ = nullptr;
        }
        return *this;
    }

    Type* operator->() const { return ref_; }

    Type& operator*() const { return *ref_; }

    Type* Get() const { return ref_; }

    operator Type*() const noexcept { return ref_; }

    bool operator!() const { return ref_ == nullptr; }

    operator bool() const noexcept { return ref_ != nullptr; }

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

}