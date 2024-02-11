#pragma once

#include <tao/pegtl.hpp>

namespace nix::parser {

// modified copy of change_state, as the manual suggest for more involved
// state manipulation. we want to change only the first state parameter,
// and we care about the *initial* position of a rule application (not the
// past-the-end position as pegtl change_state provides)
template<typename NewState>
struct change_head : tao::pegtl::maybe_nothing
{
    template<
        typename Rule,
        tao::pegtl::apply_mode A,
        tao::pegtl::rewind_mode M,
        template<typename...> class Action,
        template<typename...> class Control,
        typename ParseInput,
        typename State,
        typename... States
    >
    [[nodiscard]] static bool match(ParseInput & in, State && st, States &&... sts)
    {
        const auto begin = in.iterator();

        if constexpr (std::is_constructible_v<NewState, State, States...>) {
            NewState s(st, sts...);
            if (tao::pegtl::match<Rule, A, M, Action, Control>(in, s, sts...)) {
                if constexpr (A == tao::pegtl::apply_mode::action) {
                    _success<Action<Rule>>(0, begin, in, s, st, sts...);
                }
                return true;
            }
            return false;
        } else if constexpr (std::is_default_constructible_v<NewState>) {
            NewState s;
            if (tao::pegtl::match<Rule, A, M, Action, Control>(in, s, sts...)) {
                if constexpr (A == tao::pegtl::apply_mode::action) {
                    _success<Action<Rule>>(0, begin, in, s, st, sts...);
                }
                return true;
            }
            return false;
        } else {
            static_assert(decltype(sizeof(NewState))(), "unable to instantiate new state");
        }
    }

    template<typename Target, typename ParseInput, typename... S>
    static void _success(void *, auto & begin, ParseInput & in, S & ... sts)
    {
        const typename ParseInput::action_t at(begin, in);
        Target::success(at, sts...);
    }

    template<typename Target, typename... S>
    static void _success(decltype(Target::success0(std::declval<S &>()...), 0), auto &, auto &, S & ... sts)
    {
        Target::success0(sts...);
    }
};

}
