#include "nix/expr/primops.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/util/strings.hh"
#include "nix/util/util.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

#include <regex>

namespace nix {

static inline Value * mkString(EvalState & state, const std::csub_match & match)
{
    Value * v = state.allocValue();
    v->mkString({match.first, match.second}, state.mem);
    return v;
}

namespace {

struct RegexCache : EvalState::Extension
{
    struct Entry
    {
        ref<const std::regex> regex;

        Entry(const char * s, size_t count)
            : regex(make_ref<const std::regex>(s, count, std::regex::extended))
        {
        }
    };

    boost::concurrent_flat_map<std::string, Entry, StringViewHash, std::equal_to<>> cache;

    ref<const std::regex> get(std::string_view re)
    {
        std::optional<ref<const std::regex>> regex;
        cache.try_emplace_and_cvisit(
            re,
            /*s=*/re.data(),
            /*count=*/re.size(),
            [&regex](const auto & kv) { regex = kv.second.regex; },
            [&regex](const auto & kv) { regex = kv.second.regex; });
        return *regex;
    }
};

} // namespace

static EvalState::RegisterExtension<RegexCache> rRegexCache;

void prim_match(EvalState & state, CallSite callSite, Value * const * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.match");

    try {

        auto regex = state.getExtension(rRegexCache).get(re);

        NixStringContext context;
        const auto str = state.forceString(
            *args[1], context, noPos, "while evaluating the second argument passed to builtins.match");

        std::cmatch match;
        if (!std::regex_match(str.begin(), str.end(), match, *regex)) {
            v.mkNull();
            return;
        }

        // the first match is the whole string
        auto list = state.buildList(match.size() - 1);
        for (const auto & [i, v2] : enumerate(list))
            if (!match[i + 1].matched)
                v2 = &Value::vNull;
            else
                v2 = mkString(state, match[i + 1]);
        v.mkList(list);

    } catch (std::regex_error & e) {
        if (e.code() == std::regex_constants::error_space) {
            // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
            state.error<EvalError>("memory limit exceeded by regular expression '%s'", re).atPos(noPos).debugThrow();
        } else
            state.error<EvalError>("invalid regular expression '%s'", re).atPos(noPos).debugThrow();
    }
}

static RegisterPrimOp primop_match({
    .name = "__match",
    .args = {"regex", "str"},
    .doc = R"s(
      Returns a list if the [extended POSIX regular
      expression](http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_04)
      *regex* matches *str* precisely, otherwise returns `null`. Each item
      in the list is a regex group.

      ```nix
      builtins.match "ab" "abc"
      ```

      Evaluates to `null`.

      ```nix
      builtins.match "abc" "abc"
      ```

      Evaluates to `[ ]`.

      ```nix
      builtins.match "a(b)(c)" "abc"
      ```

      Evaluates to `[ "b" "c" ]`.

      ```nix
      builtins.match "[[:space:]]+([[:upper:]]+)[[:space:]]+" "  FOO   "
      ```

      Evaluates to `[ "FOO" ]`.
    )s",
    .impl = prim_match,
});

/* Split a string with a regular expression, and return a list of the
   non-matching parts interleaved by the lists of the matching groups. */
void prim_split(EvalState & state, CallSite callSite, Value * const * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], noPos, "while evaluating the first argument passed to builtins.split");

    try {

        auto regex = state.getExtension(rRegexCache).get(re);

        NixStringContext context;
        const auto str = state.forceString(
            *args[1], context, noPos, "while evaluating the second argument passed to builtins.split");

        auto begin = std::cregex_iterator(str.begin(), str.end(), *regex);
        auto end = std::cregex_iterator();

        // Any matches results are surrounded by non-matching results.
        const size_t len = std::distance(begin, end);
        auto list = state.buildList(2 * len + 1);
        size_t idx = 0;

        if (len == 0) {
            list[0] = args[1];
            v.mkList(list);
            return;
        }

        for (auto i = begin; i != end; ++i) {
            assert(idx <= 2 * len + 1 - 3);
            const auto & match = *i;

            // Add a string for non-matched characters.
            list[idx++] = mkString(state, match.prefix());

            // Add a list for matched substrings.
            const size_t slen = match.size() - 1;

            // Start at 1, because the first match is the whole string.
            auto list2 = state.buildList(slen);
            for (const auto & [si, v2] : enumerate(list2)) {
                if (!match[si + 1].matched)
                    v2 = &Value::vNull;
                else
                    v2 = mkString(state, match[si + 1]);
            }

            (list[idx++] = state.allocValue())->mkList(list2);

            // Add a string for non-matched suffix characters.
            if (idx == 2 * len)
                list[idx++] = mkString(state, match.suffix());
        }

        assert(idx == 2 * len + 1);

        v.mkList(list);

    } catch (std::regex_error & e) {
        if (e.code() == std::regex_constants::error_space) {
            // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
            state.error<EvalError>("memory limit exceeded by regular expression '%s'", re).atPos(noPos).debugThrow();
        } else
            state.error<EvalError>("invalid regular expression '%s'", re).atPos(noPos).debugThrow();
    }
}

static RegisterPrimOp primop_split({
    .name = "__split",
    .args = {"regex", "str"},
    .doc = R"s(
      Returns a list composed of non matched strings interleaved with the
      lists of the [extended POSIX regular
      expression](http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_04)
      *regex* matches of *str*. Each item in the lists of matched
      sequences is a regex group.

      ```nix
      builtins.split "(a)b" "abc"
      ```

      Evaluates to `[ "" [ "a" ] "c" ]`.

      ```nix
      builtins.split "([ac])" "abc"
      ```

      Evaluates to `[ "" [ "a" ] "b" [ "c" ] "" ]`.

      ```nix
      builtins.split "(a)|(c)" "abc"
      ```

      Evaluates to `[ "" [ "a" null ] "b" [ null "c" ] "" ]`.

      ```nix
      builtins.split "([[:upper:]]+)" " FOO "
      ```

      Evaluates to `[ " " [ "FOO" ] " " ]`.
    )s",
    .impl = prim_split,
});

} // namespace nix
