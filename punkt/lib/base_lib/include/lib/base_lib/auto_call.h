// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/base_lib/type_support.h>

// RAII class to automatically call a function-like thing as it goes out of
// scope
//
// Examples:
//
//    extern int foo();
//    int a;
//
//    auto ac = MakeAutoCall([&](){ a = 1; });
//    auto ac2 = MakeAutoCall(foo);
//
//    auto func = [&](){ a = 2; };
//    AutoCall<decltype(bleh)> ac3(func);
//    AutoCall<decltype(&foo)> ac4(&foo);
//
//    // abort the call
//    ac2.cancel();
namespace base_lib {

template <typename T>
class AutoCall {
public:
    constexpr explicit AutoCall(T c) : call_(move(c)) {}
    ~AutoCall() {
        call();
    }

    // move semantics
    AutoCall(AutoCall&& c) : call_(move(c.call_)), active_(c.active_) {
        c.cancel();
    }

    AutoCall& operator=(AutoCall&& c) {
        call_ = move(c.call_);
        c.cancel();
    }

    // no copy
    AutoCall(const AutoCall& c) = delete;
    AutoCall& operator=(const AutoCall& c) = delete;

    // cancel the eventual call
    void cancel() {
        active_ = false;
    }

    // call it immediately
    void call() {
        if (active_) (call_)();
        cancel();
    }

private:
    T call_;
    bool active_ = true;
};

// helper routine to create an autocall object without needing template
// specialization
template <typename T>
inline AutoCall<T> MakeAutoCall(T c) {
    return AutoCall<T>(move(c));
}

};
