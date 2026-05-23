#pragma once

#include <atomic>
#include <cstdint>

// SPECS_12 — Double-buffered snapshot with atomic pointer swap
template<typename T>
class DoubleBuffer {
public:
    DoubleBuffer() = default;

    // Writer: get write buffer, fill it, then commit
    T* write_buffer() { return &buffers_[write_idx_]; }

    void commit() {
        uint32_t next_read = write_idx_;
        read_ptr_.store(&buffers_[next_read], std::memory_order_release);
        write_idx_ = 1 - write_idx_;  // flip
    }

    // Reader: get current readable snapshot (immutable)
    const T* read() const {
        return read_ptr_.load(std::memory_order_acquire);
    }

    void reset() {
        buffers_[0] = T{};
        buffers_[1] = T{};
        write_idx_ = 0;
        read_ptr_.store(&buffers_[0], std::memory_order_release);
    }

private:
    T buffers_[2] = {};
    uint32_t write_idx_ = 0;
    std::atomic<T*> read_ptr_{&buffers_[0]};
};
