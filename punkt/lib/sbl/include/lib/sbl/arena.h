// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <new.h>
#include <stddef.h>

#include <lib/sbl/intrusive_single_list.h>
#include <lib/sbl/type_support.h>

namespace sbl {

// Arena is a fast memory allocator for objects of a single size.
// Both Alloc() and Free() are always O(1) and memory always comes
// from a single contigous chunck of page-aligned memory.
//
// The control structures and data are not interleaved so it is
// more resilient to memory bugs than traditional pool allocators.
//
// The overhead per object is two pointers (16 bytes in 64-bits)

class Arena {
public:
    Arena();
    ~Arena();

    status_t Init(const char* name, size_t ob_size, size_t max_count);
    void* Alloc();
    void Free(void* addr);
    size_t Trim();

    bool in_range(void* addr) const {
        return ((addr >= static_cast<void*>(d_start_)) &&
                (addr < static_cast<void*>(d_top_)));
    }

    void* start() const { return d_start_; }
    void* end() const { return d_end_; }

private:
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    struct Node {
        Node* next;
        void* slot;

        Node* list_next() {
            return next;
        }
        const Node* list_next() const {
            return next;
        }
        void list_set_next(Node* node) {
            next = node;
        }
    };

    SinglyLinkedList<Node> free_;

    size_t ob_size_;

    // Control region pointers.
    char* c_start_;
    char* c_top_;
    // Data region pointers.
    char* d_start_;
    char* d_top_;
    char* d_end_;
};

template <typename T>
class TypedArena {
public:
    status_t Init(const char* name, size_t max_count) {
        return arena_.Init(name, sizeof(T), max_count);
    }

    template <typename... Args>
    T* New(Args&&... args) {
        void* addr = arena_.Alloc();
        return addr ? new (addr) T(sbl::forward<Args>(args)...) : nullptr;
    };

    void Delete(T* obj) {
        obj->~T();
        arena_.Free(obj);
    }

    void RawFree(void* mem) {
        arena_.Free(mem);
    }

    bool in_range(void* obj) const { return arena_.in_range(obj); }

    void* start() const { return arena_.start(); }
    void* end() const { return arena_.end(); }

private:
    Arena arena_;
};
}
