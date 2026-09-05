#pragma once

#include <atomic>

//==============================================================================
// SpinLock
// << Correctly implementing a spinlock in C++ >>
// https://rigtorp.se/spinlock/
//==============================================================================

struct SpinLock
{
    std::atomic_bool flag = false;
    
    void lock() noexcept
    {
        while (true)
        {
            // optimistically assume the lock is free on the first try
            if (!flag.exchange(true, std::memory_order_acquire))
                return;
            // wait for lock to be released without generating cache misses
            while (flag.load(std::memory_order_relaxed)) {}
        }
    }

    bool try_lock() noexcept 
    {
        // first do a relaxed load to check if lock is free in order to prevent
        // unnecessary cache misses if someone does while(!try_lock())
        return !flag.load(std::memory_order_relaxed) &&
               !flag.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept
    {
        flag.store(false, std::memory_order_release);
    }
};