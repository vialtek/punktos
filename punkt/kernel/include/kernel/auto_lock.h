// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <lk/cpp.h>

class AutoLock {
public:
    explicit AutoLock(mutex_t *mutex) : mutex_(mutex) { mutex_acquire(mutex_); }
    AutoLock(mutex_t &mutex) : AutoLock(&mutex) {}

    explicit AutoLock(Mutex *mutex) : AutoLock(&mutex->lock_) {}
    AutoLock(Mutex &mutex) : AutoLock(&mutex) {}

    ~AutoLock() { release(); }

    // early release the mutex before the object goes out of scope
    void release() {
        if (likely(mutex_)) {
            mutex_release(mutex_);
            mutex_ = nullptr;
        }
    }

    // suppress default constructors
    DISALLOW_COPY_ASSIGN_AND_MOVE(AutoLock);

private:
    mutex_t *mutex_ = nullptr;
};
