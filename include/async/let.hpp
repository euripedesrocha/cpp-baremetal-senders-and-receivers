#pragma once

#include <async/concepts.hpp>
#include <async/env.hpp>
#include <async/forwarding_query.hpp>
#include <async/tags.hpp>
#include <async/type_traits.hpp>

#include <stdx/concepts.hpp>
#include <stdx/functional.hpp>

#include <boost/mp11/algorithm.hpp>
#include <boost/mp11/list.hpp>

#include <type_traits>
#include <utility>
#include <variant>

namespace async::_let {
template <typename F, channel_tag Tag, typename Ops, typename Rcvr>
struct receiver {
    using is_receiver = void;
    [[no_unique_address]] F f;
    Ops *ops;

  private:
    template <channel_tag OtherTag, typename... Args>
    friend auto tag_invoke(OtherTag, receiver const &self, Args &&...args)
        -> void {
        OtherTag{}(self.ops->rcvr, std::forward<Args>(args)...);
    }

    template <stdx::same_as_unqualified<receiver> Self, typename... Args>
    friend auto tag_invoke(Tag, Self &&self, Args &&...args) -> void {
        self.ops->complete_first(
            std::forward<Self>(self).f(std::forward<Args>(args)...));
    }

    [[nodiscard]] friend constexpr auto tag_invoke(async::get_env_t,
                                                   receiver const &r)
        -> detail::forwarding_env<env_of_t<Rcvr>> {
        return forward_env_of(r.ops->rcvr);
    }
};

template <typename Sndr, typename Rcvr, typename Func, channel_tag Tag,
          typename DependentSenders>
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
struct op_state {
    using first_rcvr = receiver<Func, Tag, op_state, Rcvr>;

    template <typename S, typename R, typename F>
    constexpr op_state(S &&s, R &&r, F &&f)
        : rcvr{std::forward<R>(r)},
          state{std::in_place_index<0>, stdx::with_result_of{[&] {
                    return connect(std::forward<S>(s),
                                   first_rcvr{std::forward<F>(f), this});
                }}} {}
    constexpr op_state(op_state &&) = delete;

    template <typename S> auto complete_first(S &&s) -> void {
        using index =
            boost::mp11::mp_find<DependentSenders, std::remove_cvref_t<S>>;
        static_assert(index::value <
                      boost::mp11::mp_size<DependentSenders>::value);
        auto &op =
            state.template emplace<index::value + 1>(stdx::with_result_of{
                [&] { return connect(std::forward<S>(s), rcvr); }});
        start(std::move(op));
    }

    template <typename S>
    using dependent_connect_result_t = connect_result_t<S, Rcvr>;
    using dependent_ops = boost::mp11::mp_apply<
        std::variant, boost::mp11::mp_transform<dependent_connect_result_t,
                                                DependentSenders>>;

    using first_ops = connect_result_t<Sndr, first_rcvr>;
    using state_t = boost::mp11::mp_push_front<dependent_ops, first_ops>;

    [[no_unique_address]] Rcvr rcvr;
    state_t state;

  private:
    template <stdx::same_as_unqualified<op_state> O>
    friend constexpr auto tag_invoke(start_t, O &&o) -> void {
        start(std::get<0>(std::forward<O>(o).state));
    }
};

template <typename Sndr, typename S, typename F, typename Tag> struct sender {
    using is_sender = void;

    template <typename... Ts>
    using invoked_type = std::invoke_result_t<F, Ts...>;
    template <typename E>
    using dependent_senders = detail::gather_signatures<Tag, S, E, invoked_type,
                                                        completion_signatures>;

    template <typename E> struct completions_of {
        template <typename T> using fn = completion_signatures_of_t<T, E>;
    };
    template <typename E>
    using dependent_signatures = boost::mp11::mp_unique<boost::mp11::mp_flatten<
        boost::mp11::mp_transform_q<completions_of<E>, dependent_senders<E>>>>;

    template <typename...> using signatures = completion_signatures<>;

    template <receiver_from<S> R>
    [[nodiscard]] friend constexpr auto tag_invoke(connect_t, Sndr &&s, R &&r)
        -> _let::op_state<S, std::remove_cvref_t<R>, F, Tag,
                          dependent_senders<env_of_t<R>>> {
        return {std::move(s).s, std::forward<R>(r), std::move(s).f};
    }

    template <typename R> struct is_multishot_sender {
        template <typename T>
        using fn = std::bool_constant<multishot_sender<T, R>>;
    };

    template <stdx::same_as_unqualified<Sndr> Self, receiver_from<S> R>
        requires multishot_sender<S, R> and
                 boost::mp11::mp_all_of_q<dependent_senders<env_of_t<R>>,
                                          is_multishot_sender<R>>::value
    [[nodiscard]] friend constexpr auto tag_invoke(connect_t, Self &&self,
                                                   R &&r)
        -> _let::op_state<S, std::remove_cvref_t<R>, F, Tag,
                          dependent_senders<env_of_t<R>>> {
        return {std::forward<Self>(self).s, std::forward<R>(r),
                std::forward<Self>(self).f};
    }

    template <stdx::same_as_unqualified<Sndr> Self>
    [[nodiscard]] friend constexpr auto tag_invoke(async::get_env_t,
                                                   Self &&self) {
        return forward_env_of(self.s);
    }
};

template <typename F, template <typename...> typename Sndr> struct pipeable {
    [[no_unique_address]] F f;

  private:
    template <async::sender S, stdx::same_as_unqualified<pipeable> Self>
    friend constexpr auto operator|(S &&s, Self &&self) -> async::sender auto {
        return Sndr<std::remove_cvref_t<S>, F>{
            {}, std::forward<S>(s), std::forward<Self>(self).f};
    }
};
} // namespace async::_let
