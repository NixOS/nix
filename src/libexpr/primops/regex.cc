#include "primops.hh"
#include "eval-inline.hh"
#include "derivations.hh"
#include "store-api.hh"
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>


namespace nix {

MakeError(RegexError, Error);
namespace PCRE {

class MatchData;

class Regex
{
    friend class MatchData;
protected:
    pcre2_code* code;
    size_t usage = 0;
    std::vector<std::pair<std::string_view, uint32_t>> name_table;
public:
    Regex(std::string_view re)
    {
        int errorcode;
        PCRE2_SIZE erroffset;

        code = pcre2_compile((const unsigned char*)re.data(), re.length(), 0, &errorcode, &erroffset, nullptr);

        if (code == nullptr) {
            unsigned char err[256];
            pcre2_get_error_message(errorcode, err, sizeof(err));
            throw RegexError("unable to compile regex: %1% at offset %2%", err, erroffset);
        }
        // parse nametable
        uint32_t namecount;
        pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &namecount);
        if (namecount) {
            PCRE2_SPTR tabptr;
            uint32_t name_entry_size;
            pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, &tabptr);
            pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &name_entry_size);
            name_table.reserve(namecount);
            for (size_t i = 0; i < namecount; i++) {
                int n = tabptr[0] << 8 | tabptr[1];
                name_table.emplace_back((const char*)(tabptr+2), n);
                tabptr += name_entry_size;
            }
        }
    }
    Regex(const Regex&) = delete;
    Regex(Regex&&) = delete; // move not implemented
    ~Regex()
    {
        pcre2_code_free(code);
    };

    const decltype(name_table)& nameTable() const {
        return name_table;
    };

    int match(const std::string_view str, MatchData& match, PCRE2_SIZE startoffset = 0, uint32_t options = 0);
    uint32_t captureCount() noexcept
    {
        uint32_t len;
        pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &len);
        return len;
    }

    void compile()
    {
        assert(pcre2_jit_compile(code, PCRE2_JIT_COMPLETE) == 0);
    }
};

class MatchData
{
    friend class Regex;
protected:
    pcre2_match_data* match;
    std::string_view str;
    uint32_t size_;
    PCRE2_SIZE* ovector;
    Regex& re;
public:
    MatchData(Regex& re) noexcept
        : re(re)
    {
        match = pcre2_match_data_create_from_pattern(re.code, NULL);
        size_ = pcre2_get_ovector_count(match);
        ovector = pcre2_get_ovector_pointer(match);
    };
    MatchData(const MatchData&) = delete;
    MatchData(MatchData&&) = delete;
    ~MatchData()
    {
        pcre2_match_data_free(match);
    };

    const Regex& regex() const noexcept
    {
        return re;
    };

    uint32_t size() const noexcept
    {
        return size_;
    };

    std::optional<std::string_view> operator[](std::size_t i) const
    {
        assert(i < size());
        if (ovector[2*i] == PCRE2_UNSET) return {};
        return str.substr(ovector[2*i], ovector[2*i+1] - ovector[2*i]);
    };

    uint32_t startPos() const noexcept
    {
        return pcre2_get_startchar(match);
    };
};

int Regex::match(std::string_view str, MatchData& match, PCRE2_SIZE startoffset, uint32_t options)
{
    // Cache the string in the match data for match[] to work.
    match.str = str;
    // compile if we're using this regex more than once
    if (this->usage++) this->compile();

    int rc = pcre2_match(code, (const unsigned char*)str.data(), str.length(),
        startoffset, options, match.match, NULL);

    assert(rc != 0); // match data too small, shouldn't happen

    if (rc < 0 && rc != PCRE2_ERROR_NOMATCH) {
        unsigned char err[256];
        pcre2_get_error_message(rc, err, sizeof(err));
        throw RegexError("unable to match regex: %1%", err);
    }
    return rc;
};

};

struct RegexCache
{
    // TODO use C++20 transparent comparison when available
    std::unordered_map<std::string_view, PCRE::Regex> cache;
    std::list<std::string> keys;

    PCRE::Regex& get(std::string_view re)
    {
        auto it = cache.find(re);
        if (it != cache.end())
            return it->second;
        keys.emplace_back(re);
        return cache.emplace(keys.back(), keys.back()).first->second;
    }
};

std::shared_ptr<RegexCache> makeRegexCache()
{
    return std::make_shared<RegexCache>();
}

size_t regexCacheSize(std::shared_ptr<RegexCache> cache)
{
    return cache->keys.size();
}

static void extract_matches(EvalState & state, const PCRE::MatchData& match, Value & v)
{
    auto nameTable = match.regex().nameTable();
    if (nameTable.size()) {
        // try to extract named bindings
        auto bindings = state.buildBindings(nameTable.size());
        for (auto i : nameTable) {
            Value & elem = bindings.alloc(i.first);
            if (!match[i.second].has_value()) elem.mkNull();
            else elem.mkString(*match[i.second]);
        }
        v.mkAttrs(bindings);
    } else {
        // the first match is the whole string
        const size_t len = match.size() - 1;
        state.mkList(v, len);
        for (size_t i = 0; i < len; ++i) {
            if (!match[i+1].has_value())
                (v.listElems()[i] = state.allocValue())->mkNull();
            else
                (v.listElems()[i] = state.allocValue())->mkString(*match[i + 1]);
        }
    }
}

void prim_match(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], pos);

    try {

        auto& regex = state.regexCache->get(re);

        PathSet context;
        const auto str = state.forceString(*args[1], context, pos);

        PCRE::MatchData match(regex);
        int rc = regex.match(str, match, 0, PCRE2_ANCHORED | PCRE2_ENDANCHORED);
        if (rc == PCRE2_ERROR_NOMATCH) {
            v.mkNull();
            return;
        }
        extract_matches(state, match, v);

    } catch (RegexError & e) {
        state.debugThrowLastTrace(EvalError({
            .msg = hintfmt("error while evaluating regex '%s': ", re, e.what()),
            .errPos = state.positions[pos]
        }));
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
    .fun = prim_match,
});

/* Split a string with a regular expression, and return a list of the
   non-matching parts interleaved by the lists of the matching groups. */
void prim_split(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], pos);

    try {
        auto& regex = state.regexCache->get(re);

        // We're going to use this regex many times, JIT it.
        regex.compile();

        PathSet context;
        const auto str = state.forceString(*args[1], context, pos);


        PCRE::MatchData match(regex);
        int rc = regex.match(str, match);

        if (rc == PCRE2_ERROR_NOMATCH) {
            state.mkList(v, 1);
            v.listElems()[0] = args[1];
            return;
        }
        ValueVector result;
        result.reserve(3);
        size_t lastmatchend = 0;
        size_t newmatchstart = 0;
        while (rc != PCRE2_ERROR_NOMATCH && newmatchstart <= str.length()) {
            // Add a string for non-matched characters.
            Value * prefix = state.allocValue();
            prefix->mkString(str.substr(lastmatchend, match.startPos() - lastmatchend));
            result.push_back(prefix);

            // Add a list for matched substrings.
            auto elem = state.allocValue();
            result.push_back(elem);

            extract_matches(state, match, *elem);

            lastmatchend = match.startPos() + match[0]->length();
            newmatchstart = lastmatchend + (match[0]->length() == 0 ? 1 : 0);
            if (newmatchstart <= str.length())
                rc = regex.match(str, match, newmatchstart);
        }

        // Add a string for non-matched suffix characters.
        Value * rest = state.allocValue();
        rest->mkString(str.substr(lastmatchend, -1));
        result.push_back(rest);

        state.mkList(v, result.size());
        for (size_t n = 0; n < result.size(); ++n)
            v.listElems()[n] = result[n];

    } catch (RegexError & e) {
        state.debugThrowLastTrace(EvalError({
            .msg = hintfmt("error while evaluating regex '%s': ", re, e.what()),
            .errPos = state.positions[pos]
        }));
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
    .fun = prim_split,
});

}
