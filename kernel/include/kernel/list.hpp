#pragma once

#include <cstdint>
#include <cstddef>

namespace myrtos {

class IntrusiveList;
struct ListNode {
    IntrusiveList* list{nullptr};
    ListNode* next{nullptr};
    ListNode* prev{nullptr};
    uint32_t value{0};   // Used for sorted insertion (e.g., wake tick or priority)
    void* owner{nullptr}; // Pointer back to containing object (e.g., TCB)

    constexpr ListNode() noexcept = default;

    constexpr explicit ListNode(void* owner_obj, uint32_t val = 0) noexcept
        : next(nullptr), prev(nullptr), value(val), owner(owner_obj) {}

    [[nodiscard]] constexpr bool is_linked() const noexcept {
        return next != nullptr;
    }
};

/**
 * @brief Intrusive circular doubly-linked list.
 * 
 * Provides deterministic O(1) append, prepend, remove operations without dynamic memory allocation.
 */
class IntrusiveList {
public:
    constexpr IntrusiveList() noexcept : root_(&root_, &root_), count_(0) {}

    // Disable copying to prevent corrupted link chains
    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;

    IntrusiveList(IntrusiveList&& other) noexcept : IntrusiveList() {
        while (auto* node = other.pop_front()) push_back(node);
    }
    IntrusiveList& operator=(IntrusiveList&& other) noexcept {
        if (this != &other) { reset(); while (auto* node = other.pop_front()) push_back(node); }
        return *this;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return count_ == 0;
    }

    [[nodiscard]] constexpr size_t size() const noexcept {
        return count_;
    }

    [[nodiscard]] ListNode* head() const noexcept {
        return empty() ? nullptr : root_.next;
    }

    [[nodiscard]] ListNode* tail() const noexcept {
        return empty() ? nullptr : root_.prev;
    }

    void push_back(ListNode* node) noexcept {
        if (node == nullptr) return;
        insert_before(&root_, node);
    }

    void push_front(ListNode* node) noexcept {
        if (node == nullptr) return;
        insert_after(&root_, node);
    }

    ListNode* pop_front() noexcept {
        if (empty()) return nullptr;
        ListNode* first = root_.next;
        remove(first);
        return first;
    }

    void remove(ListNode* node) noexcept {
        if (node == nullptr || node->list != this) return;
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->next = nullptr;
        node->prev = nullptr;
        node->list = nullptr;
        if (count_ > 0) {
            --count_;
        }
    }

    /**
     * @brief Inserts node in ascending order based on `node->value`.
     * Useful for delta-queues and priority-sorted wait lists.
     */
    void insert_sorted_ascending(ListNode* node) noexcept {
        if (node == nullptr) return;
        ListNode* curr = root_.next;
        while (curr != &root_ && curr->value <= node->value) {
            curr = curr->next;
        }
        insert_before(curr, node);
    }

    /**
     * @brief Inserts node in descending order based on `node->value`.
     */
    void insert_sorted_descending(ListNode* node) noexcept {
        if (node == nullptr) return;
        ListNode* curr = root_.next;
        while (curr != &root_ && curr->value >= node->value) {
            curr = curr->next;
        }
        insert_before(curr, node);
    }

    void reset() noexcept {
        while (!empty()) pop_front();
        root_.next = &root_;
        root_.prev = &root_;
        count_ = 0;
    }

private:
    void insert_before(ListNode* pos, ListNode* node) noexcept {
        if (node->is_linked()) return;
        node->list = this;
        node->next = pos;
        node->prev = pos->prev;
        pos->prev->next = node;
        pos->prev = node;
        ++count_;
    }

    void insert_after(ListNode* pos, ListNode* node) noexcept {
        if (node->is_linked()) return;
        node->list = this;
        node->prev = pos;
        node->next = pos->next;
        pos->next->prev = node;
        pos->next = node;
        ++count_;
    }

    struct RootNode : public ListNode {
        constexpr RootNode() noexcept {
            next = this;
            prev = this;
        }
        constexpr RootNode(ListNode* n, ListNode* p) noexcept {
            next = n;
            prev = p;
        }
    };

    RootNode root_{&root_, &root_};
    size_t count_{0};
};

} // namespace myrtos
