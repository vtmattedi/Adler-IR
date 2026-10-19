#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/// @brief Holds a FreeRTOS mutex for one scope and releases it on every exit path.
///
/// Not usable from an ISR (it takes with xSemaphoreTake). A null handle or a timed-out take leaves
/// the guard not owning the lock, so check owns() whenever the timeout is not portMAX_DELAY:
///
///   {
///       adler::Lock lock(mutex_);          // blocks until taken
///       queue_.push(item);
///   }                                      // released here
///
///   adler::Lock lock(mutex_, 0);           // never blocks
///   if (!lock.owns()) return;
class Lock
{
public:
    explicit Lock(SemaphoreHandle_t mutex, TickType_t timeout = portMAX_DELAY) : mutex_(mutex)
    {
        owns_ = mutex_ != nullptr && xSemaphoreTake(mutex_, timeout) == pdTRUE;
    }

    ~Lock() { unlock(); }

    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;

    /// @brief True when this guard currently holds the mutex.
    bool owns() const { return owns_; }
    explicit operator bool() const { return owns_; }

    /// @brief Releases early, e.g. before calling out to a callback. Safe to call twice.
    void unlock()
    {
        if (owns_)
        {
            xSemaphoreGive(mutex_);
            owns_ = false;
        }
    }

private:
    SemaphoreHandle_t mutex_;
    bool owns_ = false;
};
