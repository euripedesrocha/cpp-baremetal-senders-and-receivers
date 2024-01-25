#include "detail/common.hpp"

#include <async/concepts.hpp>
#include <async/just.hpp>
#include <async/tags.hpp>

#include <catch2/catch_test_macros.hpp>

#include <utility>

TEST_CASE("one value", "[just_stopped]") {
    int value{};
    auto s = async::just_stopped();
    auto op = async::connect(s, stopped_receiver{[&] { value = 42; }});
    async::start(op);
    CHECK(value == 42);
}

TEST_CASE("just_stopped advertises what it sends", "[just_stopped]") {
    static_assert(async::sender_of<decltype(async::just_stopped()),
                                   async::set_stopped_t()>);
}

TEST_CASE("copy sender", "[just_stopped]") {
    int value{};
    auto const s = async::just_stopped();
    static_assert(async::multishot_sender<decltype(s), universal_receiver>);
    auto op = async::connect(s, stopped_receiver{[&] { value = 42; }});
    async::start(op);
    CHECK(value == 42);
}

TEST_CASE("move sender", "[just_stopped]") {
    int value{};
    auto s = async::just_stopped();
    static_assert(async::multishot_sender<decltype(s), universal_receiver>);
    auto op =
        async::connect(std::move(s), stopped_receiver{[&] { value = 42; }});
    async::start(op);
    CHECK(value == 42);
}
