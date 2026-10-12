#pragma once

#include "kernel/types.hpp"
#include "port/port.hpp"

namespace myrtos {

/**
 * @brief O(1) Fixed-Size Block Memory Pool Allocator.
 * 
 * Guarantees zero heap fragmentation and constant-time allocation and deallocation.
 */
class MemoryPool {
public:
    MemoryPool(
        uint8_t* storage_buffer,
        size_t block_size,
        size_t block_count
    ) noexcept;

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    /// Allocates one fixed-size block in O(1) time. Returns nullptr if exhausted.
    void* allocate() noexcept;

    /// Returns a block to the pool in O(1) time.
    Status deallocate(void* block_ptr) noexcept;

    [[nodiscard]] size_t block_size() const noexcept { return block_size_; }
    [[nodiscard]] size_t total_blocks() const noexcept { return block_count_; }
    [[nodiscard]] size_t free_blocks() const noexcept { return free_count_; }
    [[nodiscard]] size_t allocated_blocks() const noexcept { return block_count_ - free_count_; }

private:
    struct FreeNode {
        FreeNode* next{nullptr};
    };

    uint8_t* buffer_{nullptr};
    size_t block_size_{0};
    size_t block_count_{0};
    size_t free_count_{0};
    FreeNode* free_head_{nullptr};
};

/**
 * @brief Statically allocated typed Memory Pool.
 */
template <typename T, size_t BlockCount>
class StaticMemoryPool {
public:
    StaticMemoryPool() noexcept
        : pool_(reinterpret_cast<uint8_t*>(storage_), sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T), BlockCount) {}

    T* allocate() noexcept {
        return reinterpret_cast<T*>(pool_.allocate());
    }

    Status deallocate(T* ptr) noexcept {
        return pool_.deallocate(ptr);
    }

    [[nodiscard]] size_t free_count() const noexcept { return pool_.free_blocks(); }
    [[nodiscard]] size_t total_count() const noexcept { return BlockCount; }

private:
    alignas(alignof(T) > alignof(void*) ? alignof(T) : alignof(void*))
    uint8_t storage_[(sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T)) * BlockCount];
    MemoryPool pool_;
};

} // namespace myrtos
