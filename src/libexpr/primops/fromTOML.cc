#include "nix/expr/primops.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/static-string-data.hh"

#include "expr-config-private.hh"

#include <sstream>

#include <toml.hpp>

namespace nix {

#if HAVE_TOML11_4

/**
 * This is what toml11 < 4.0 did when choosing the subsecond precision.
 * TOML 1.0.0 spec doesn't define how sub-millisecond ranges should be handled and calls it
 * implementation defined behavior. For a lack of a better choice we stick with what older versions
 * of toml11 did [1].
 *
 * [1]: https://github.com/ToruNiina/toml11/blob/dcfe39a783a94e8d52c885e5883a6fbb21529019/toml/datetime.hpp#L282
 */
static size_t normalizeSubsecondPrecision(toml::local_time lt)
{
    auto millis = lt.millisecond;
    auto micros = lt.microsecond;
    auto nanos = lt.nanosecond;
    if (millis != 0 || micros != 0 || nanos != 0) {
        if (micros != 0 || nanos != 0) {
            if (nanos != 0)
                return 9;
            return 6;
        }
        return 3;
    }
    return 0;
}

/**
 * Normalize date/time formats to serialize to the same strings as versions prior to toml11 4.0.
 *
 * Several things to consider:
 *
 * 1. Sub-millisecond range is represented the same way as in toml11 versions prior to 4.0. Precisioun is rounded
 *    towards the next multiple of 3 or capped at 9 digits.
 * 2. Seconds must be specified. This may become optional in (yet unreleased) TOML 1.1.0, but 1.0.0 defined local time
 *    in terms of RFC3339 [1].
 * 3. date-time separator (`t`, `T` or space ` `) is canonicalized to an upper T. This is compliant with RFC3339
 *    [1] 5.6:
 *    > Applications that generate this format SHOULD use upper case letters.
 *
 * [1]: https://datatracker.ietf.org/doc/html/rfc3339#section-5.6
 */
static void normalizeDatetimeFormat(toml::value & t)
{
    if (t.is_local_datetime()) {
        auto & ldt = t.as_local_datetime();
        t.as_local_datetime_fmt() = {
            .delimiter = toml::datetime_delimiter_kind::upper_T,
            // https://datatracker.ietf.org/doc/html/rfc3339#section-5.6
            .has_seconds = true, // Mandated by TOML 1.0.0
            .subsecond_precision = normalizeSubsecondPrecision(ldt.time),
        };
        return;
    }

    if (t.is_offset_datetime()) {
        auto & odt = t.as_offset_datetime();
        t.as_offset_datetime_fmt() = {
            .delimiter = toml::datetime_delimiter_kind::upper_T,
            // https://datatracker.ietf.org/doc/html/rfc3339#section-5.6
            .has_seconds = true, // Mandated by TOML 1.0.0
            .subsecond_precision = normalizeSubsecondPrecision(odt.time),
        };
        return;
    }

    if (t.is_local_time()) {
        auto & lt = t.as_local_time();
        t.as_local_time_fmt() = {
            .has_seconds = true, // Mandated by TOML 1.0.0
            .subsecond_precision = normalizeSubsecondPrecision(lt),
        };
        return;
    }
}

#endif

static void prim_fromTOML(EvalState & state, const PosIdx pos, Value ** args, Value & val)
{
    auto toml = state.forceStringNoCtx(*args[0], pos, "while evaluating the argument passed to builtins.fromTOML");

    std::istringstream tomlStream(std::string{toml});

    auto visit = [&](this auto & self, Value & v, toml::value t) -> void {
        switch (t.type()) {
        case toml::value_t::table: {
            auto table = toml::get<toml::table>(t);
            auto attrs = state.buildBindings(table.size());

            for (auto & elem : table) {
                forceNoNullByte(elem.first);
                self(attrs.alloc(elem.first), elem.second);
            }

            v.mkAttrs(attrs);
        } break;
        case toml::value_t::array: {
            auto array = toml::get<std::vector<toml::value>>(t);

            auto list = state.buildList(array.size());
            for (const auto & [n, v] : enumerate(list))
                self(*(v = state.allocValue()), array[n]);
            v.mkList(list);
        } break;
        case toml::value_t::boolean:
            v.mkBool(toml::get<bool>(t));
            break;
        case toml::value_t::integer:
            v.mkInt(toml::get<int64_t>(t));
            break;
        case toml::value_t::floating:
            v.mkFloat(toml::get<NixFloat>(t));
            break;
        case toml::value_t::string: {
            auto s = toml::get<std::string_view>(t);
            forceNoNullByte(s);
            v.mkString(s);
        } break;
        case toml::value_t::local_datetime:
        case toml::value_t::offset_datetime:
        case toml::value_t::local_date:
        case toml::value_t::local_time: {
            if (experimentalFeatureSettings.isEnabled(Xp::ParseTomlTimestamps)) {
#if HAVE_TOML11_4
                normalizeDatetimeFormat(t);
#endif
                auto attrs = state.buildBindings(2);
                attrs.alloc("_type").mkStringNoCopy("timestamp"_sds);
                std::ostringstream s;
                s << t;
                auto str = s.view();
                forceNoNullByte(str);
                attrs.alloc("value").mkString(str);
                v.mkAttrs(attrs);
            } else {
                throw std::runtime_error("Dates and times are not supported");
            }
        } break;
        case toml::value_t::empty:
            v.mkNull();
            break;
        }
    };

    try {
        visit(
            val,
            toml::parse(
                tomlStream,
                "fromTOML" /* the "filename" */
#if HAVE_TOML11_4
                ,
                toml::spec::v(1, 0, 0) // Be explicit that we are parsing TOML 1.0.0 without extensions
#endif
                ));
    } catch (std::exception & e) { // TODO: toml::syntax_error
        state.error<EvalError>("while parsing TOML: %s", e.what()).atPos(pos).debugThrow();
    }
}

static RegisterPrimOp primop_fromTOML(
    {.name = "fromTOML",
     .args = {"e"},
     .doc = R"(
      Convert a TOML string to a Nix value. For example,

      ```nix
      builtins.fromTOML ''
        x=1
        s="a"
        [table]
        y=2
      ''
      ```

      returns the value `{ s = "a"; table = { y = 2; }; x = 1; }`.
    )",
     .fun = prim_fromTOML});

} // namespace nix
