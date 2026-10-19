#pragma once
#include <stddef.h>
#include <Core/Logs.h>
constexpr size_t MAX_FIFO_SIZE = 10;

template <typename T, size_t N = MAX_FIFO_SIZE>
class FiFo {
    static_assert(N > 0, "FiFo size must be > 0");

    T dataArray[N];
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;

public:
    bool push(const T& item)
    {
        if (count == N) {
            LOG_ERROR("FIFO", "FIFO is full, cannot push item");
            return false;
        }
        dataArray[tail] = item;
        tail = (tail + 1) % N;
        count++;
        return true;
    }

    bool pop(T& item)
    {
        if (count == 0) {
            LOG_ERROR("FIFO", "FIFO is empty, cannot pop item");
            return false;
        }
        item = dataArray[head];
        head = (head + 1) % N;
        count--;
        return true;
    }

    /// Copies the oldest item without removing it. Silent when empty: callers poll this.
    bool peek(T& item) const
    {
        if (count == 0)
            return false;
        item = dataArray[head];
        return true;
    }

    bool isEmpty() const { return count == 0; }
};
