#include "detail/common.hpp"

#include <async/concepts.hpp>
#include <async/connect.hpp>
#include <async/just.hpp>
#include <async/retry.hpp>
#include <async/schedulers/inline_scheduler.hpp>
#include <async/schedulers/thread_scheduler.hpp>
#include <async/sequence.hpp>
#include <async/start_on.hpp>
#include <async/then.hpp>
#include <async/variant_sender.hpp>
#include <async/when_all.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("retry advertises what it sends", "[retry]") {
    [[maybe_unused]] auto s = async::just(42) | async::retry();
    static_assert(
        std::same_as<async::completion_signatures_of_t<decltype(s)>,
                     async::completion_signatures<async::set_value_t(int)>>);
}

TEST_CASE("retry advertises what it sends (nothing!)", "[retry]") {
    [[maybe_unused]] auto s = async::just_error(42) | async::retry();
    static_assert(std::same_as<async::completion_signatures_of_t<decltype(s)>,
                               async::completion_signatures<>>);
}

TEST_CASE("retry advertises sending stopped", "[retry]") {
    int var{};
    [[maybe_unused]] auto s =
        async::make_variant_sender(
            var == 0, [] { return async::just_error(42); },
            [] { return async::just_stopped(); }) |
        async::retry();
    [[maybe_unused]] auto r = stoppable_receiver([] {});
    static_assert(
        std::same_as<async::completion_signatures_of_t<
                         decltype(s), async::env_of_t<decltype(r)>>,
                     async::completion_signatures<async::set_stopped_t()>>);
}

TEST_CASE("retry propagates forwarding queries to its child environment",
          "[retry]") {
    auto s = custom_sender{};
    CHECK(get_fwd(async::get_env(s)) == 42);

    auto r = async::retry(s);
    CHECK(get_fwd(async::get_env(r)) == 42);
}

TEST_CASE("retry retries on error", "[retry]") {
    int var{};

    auto sub = async::just() | async::sequence([&] {
                   return async::make_variant_sender(
                       ++var == 2, [] { return async::just(42); },
                       [] { return async::just_error(17); });
               });
    auto s = sub | async::retry();
    auto op = async::connect(s, receiver{[&](auto i) { var += i; }});
    async::start(op);
    CHECK(var == 44);
}

TEST_CASE("retry_until advertises what it sends", "[retry]") {
    [[maybe_unused]] auto s =
        async::just_error(42) | async::retry_until([](auto) { return true; });
    static_assert(
        std::same_as<async::completion_signatures_of_t<decltype(s)>,
                     async::completion_signatures<async::set_error_t(int)>>);
}

TEST_CASE("retry_until retries on error", "[retry]") {
    int var{};

    auto sub = async::just() | async::sequence([&] {
                   ++var;
                   return async::just_error(17);
               });
    auto s = sub | async::retry_until([&](auto i) { return i + var == 20; });
    auto op = async::connect(s, receiver{[] {}});
    async::start(op);
    CHECK(var == 3);
}

TEST_CASE("retry can be cancelled", "[retry]") {
    int var{};
    stoppable_receiver r{[&] { var += 42; }};

    auto sub = async::just() | async::sequence([&] {
                   if (++var == 2) {
                       r.request_stop();
                   }
                   return async::just_error(17);
               });
    auto s = async::when_all(sub, stoppable_just()) | async::retry();
    auto op = async::connect(s, r);
    async::start(op);
    CHECK(var == 44);
}

TEST_CASE("retry may complete synchronously", "[retry]") {
    int var{};
    auto sub = async::just() | async::then([&] { ++var; });
    auto s = async::retry_until(sub, [&](auto i) { return i == 42; });
    static_assert(async::synchronous<decltype(s)>);
}

TEST_CASE("retry may not complete synchronously", "[retry]") {
    int var{};
    auto sub =
        async::thread_scheduler::schedule() | async::then([&] { ++var; });
    auto s = async::retry_until(sub, [&](auto i) { return i == 42; });
    static_assert(not async::synchronous<decltype(s)>);
}

TEST_CASE("retry op state may be synchronous", "[retry]") {
    int var{};
    auto sub = async::just() | async::then([&] {
                   ++var;
                   return var;
               });
    auto s = async::retry_until(sub, [&](auto i) { return i == 42; });
    auto op = async::connect(s, receiver{[] {}});
    static_assert(async::synchronous<decltype(op)>);
}

TEST_CASE("retry op state may not be synchronous", "[retry]") {
    int var{};
    auto sub = async::thread_scheduler::schedule() | async::then([&] {
                   ++var;
                   return var;
               });
    auto s = async::retry_until(sub, [&](auto i) { return i == 42; });
    auto op = async::connect(s, receiver{[] {}});
    static_assert(not async::synchronous<decltype(op)>);
}
