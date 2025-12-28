// tests/beman/task/task.test.cpp                                     -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/task/task.hpp>
#include <beman/execution/execution.hpp>
#include <cassert>
#include <cstdlib>
#include <new>

namespace ex = beman::execution;

// ----------------------------------------------------------------------------
// Allocation tracking

static std::size_t allocation_count = 0;

void* operator new(std::size_t size) {
    ++allocation_count;
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

// ----------------------------------------------------------------------------

namespace {
auto test_co_return() {
    auto rc = ex::sync_wait([]() -> ex::task<int> { co_return 17; }());
    assert(rc);
    [[maybe_unused]] auto [value] = rc.value_or(std::tuple{0});
    assert(value == 17);
}

auto test_cancel() {
    ex::sync_wait([]() -> ex::task<> {
        bool stopped{};
        co_await ([]() -> ex::task<void> { co_await ex::just_stopped(); }() |
                              ex::upon_stopped([&stopped]() { stopped = true; }));
        assert(stopped);
    }());
}

auto test_indirect_cancel() {
    // This approach uses symmetric transfer
    ex::sync_wait([]() -> ex::task<> {
        bool stopped{};
        co_await ([]() -> ex::task<void> { co_await []() -> ex::task<void> { co_await ex::just_stopped(); }(); }() |
                              ex::upon_stopped([&stopped]() { stopped = true; }));
        assert(stopped);
    }());
}

auto test_affinity() {
    std::cout << "test_affinity\n";
    ex::sync_wait([]() -> ex::task<> {
        co_await []() -> ex::task<> {
            ex::inline_scheduler sched{};
            std::cout << "comparing schedulers=" << std::boolalpha
                      << (sched == co_await ex::read_env(ex::get_scheduler)) << "\n";
            std::cout << "changing scheduler\n";
            co_await ex::change_coroutine_scheduler(sched);
            std::cout << "changed scheduler\n";
            co_return;
        }();
    }());
}

auto inner_task() -> ex::task<int>
{
    co_return 42;
}

auto outer_task() -> ex::task<>
{
    auto sched = co_await ex::read_env(ex::get_scheduler);
    ex::inline_scheduler inline_sched{};
    std::cout << "test_alloc: scheduler is inline_scheduler = " << std::boolalpha 
              << (sched == inline_sched) << "\n";
    co_await inner_task();
}

void test_alloc()
{
    allocation_count = 0;
    
    ex::sync_wait(outer_task());
    
    std::cout << "test_alloc: allocations when one task awaits another = " << allocation_count << "\n";
}

auto inner_task_inline() -> ex::task<int>
{
    co_return 42;
}

auto outer_task_inline() -> ex::task<>
{
    ex::inline_scheduler sched{};
    co_await ex::change_coroutine_scheduler(sched);
    
    auto current_sched = co_await ex::read_env(ex::get_scheduler);
    std::cout << "test_alloc_inline: scheduler is inline_scheduler = " << std::boolalpha 
              << (current_sched == sched) << "\n";
    
    co_await inner_task_inline();
}

void test_alloc_inline()
{
    allocation_count = 0;
    
    ex::sync_wait(outer_task_inline());
    
    std::cout << "test_alloc_inline: allocations when one task awaits another = " << allocation_count << "\n";
}

} // namespace

auto main() -> int {
    test_co_return();
    test_cancel();
    test_indirect_cancel();
    test_affinity();
    test_alloc();
    test_alloc_inline();
}
