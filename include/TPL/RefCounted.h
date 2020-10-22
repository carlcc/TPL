//
// Copyright (c) 2020 Carl Chen. All rights reserved.
//

#pragma once

#include <atomic>
#include <cassert>

namespace tpl {

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

}