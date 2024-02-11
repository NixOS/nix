#pragma once

#include "tao/pegtl.hpp"
#include <type_traits>
#include <variant>

#include <boost/container/small_vector.hpp>

// NOTE
// nix line endings are \n, \r\n, \r. the grammar does not use eol or
// eolf rules in favor of reproducing the old flex lexer as faithfully as
// possible, and deferring calculation of positions to downstream users.

namespace nix::parser::grammar {

using namespace tao::pegtl;
namespace p = tao::pegtl;

// character classes
namespace c {

struct path : sor<
    ranges<'a', 'z', 'A', 'Z', '0', '9'>,
    one<'.', '_', '-', '+'>
> {};
struct path_sep : one<'/'> {};

struct id_first : ranges<'a', 'z', 'A', 'Z', '_'> {};
struct id_rest : sor<
    ranges<'a', 'z', 'A', 'Z', '0', '9'>,
    one<'_', '\'', '-'>
> {};

struct uri_scheme_first : ranges<'a', 'z', 'A', 'Z'> {};
struct uri_scheme_rest : sor<
    ranges<'a', 'z', 'A', 'Z', '0', '9'>,
    one<'+', '-', '.'>
> {};
struct uri_sep : one<':'> {};
struct uri_rest : sor<
    ranges<'a', 'z', 'A', 'Z', '0', '9'>,
    one<'%', '/', '?', ':', '@', '&', '=', '+', '$', ',', '-', '_', '.', '!', '~', '*', '\''>
> {};

}

// "tokens". PEGs don't really care about tokens, we merely use them as a convenient
// way of writing down keywords and a couple complicated syntax rules.
namespace t {

struct _extend_as_path : seq<
    star<c::path>,
    not_at<TAO_PEGTL_STRING("/*")>,
    not_at<TAO_PEGTL_STRING("//")>,
    c::path_sep,
    sor<c::path, TAO_PEGTL_STRING("${")>
> {};
struct _extend_as_uri : seq<
    star<c::uri_scheme_rest>,
    c::uri_sep,
    c::uri_rest
> {};

// keywords might be extended to identifiers, paths, or uris.
// NOTE this assumes that keywords are a-zA-Z only, otherwise uri schemes would never
// match correctly.
// NOTE not a simple seq<...> because this would report incorrect positions for
// keywords used inside must<> if a prefix of the keyword matches.
template<typename S>
struct _keyword : sor<
    seq<
        S,
        not_at<c::id_rest>,
        not_at<_extend_as_path>,
        not_at<_extend_as_uri>
    >,
    failure
> {};

struct kw_if      : _keyword<TAO_PEGTL_STRING("if")> {};
struct kw_then    : _keyword<TAO_PEGTL_STRING("then")> {};
struct kw_else    : _keyword<TAO_PEGTL_STRING("else")> {};
struct kw_assert  : _keyword<TAO_PEGTL_STRING("assert")> {};
struct kw_with    : _keyword<TAO_PEGTL_STRING("with")> {};
struct kw_let     : _keyword<TAO_PEGTL_STRING("let")> {};
struct kw_in      : _keyword<TAO_PEGTL_STRING("in")> {};
struct kw_rec     : _keyword<TAO_PEGTL_STRING("rec")> {};
struct kw_inherit : _keyword<TAO_PEGTL_STRING("inherit")> {};
struct kw_or      : _keyword<TAO_PEGTL_STRING("or")> {};

// `-` can be a unary prefix op, a binary infix op, or the first character
// of a path or -> (ex 1->1--1)
// `/` can be a path leader or an operator (ex a?a /a)
struct op_minus : seq<one<'-'>, not_at<one<'>'>>, not_at<_extend_as_path>> {};
struct op_div   : seq<one<'/'>, not_at<c::path>> {};

// match a rule, making sure we are not matching it where a keyword would match.
// using minus like this is a lot faster than flipping the order and using seq.
template<typename... Rules>
struct _not_at_any_keyword : minus<
    seq<Rules...>,
    sor<
        TAO_PEGTL_STRING("inherit"),
        TAO_PEGTL_STRING("assert"),
        TAO_PEGTL_STRING("else"),
        TAO_PEGTL_STRING("then"),
        TAO_PEGTL_STRING("with"),
        TAO_PEGTL_STRING("let"),
        TAO_PEGTL_STRING("rec"),
        TAO_PEGTL_STRING("if"),
        TAO_PEGTL_STRING("in"),
        TAO_PEGTL_STRING("or")
    >
> {};

// identifiers are kind of horrid:
//
//   - uri_scheme_first ⊂ id_first
//   - uri_scheme_first ⊂ uri_scheme_rest ⊂ path
//   - id_first ⊂ id_rest ∖ { ' } ⊂ path
//   - id_first ∩ (path ∖ uri_scheme_first) = { _ }
//   - uri_sep ∉ ⋃ { id_first, id_rest, uri_scheme_first, uri_scheme_rest, path }
//   - path_sep ∉ ⋃ { id_first, id_rest, uri_scheme_first, uri_scheme_rest }
//
// and we want, without reading the input more than once, a string that
// matches (id_first id_rest*) and is not followed by any number of
// characters such that the extended string matches path or uri rules.
//
// since the first character must be either _ or a uri scheme character
// we can ignore path-like bits at the beginning. uri_sep cannot appear anywhere
// in an identifier, so it's only needed in lookahead checks at the uri-like
// prefix. likewise path_sep cannot appear anywhere in the idenfier, so it's
// only needed in lookahead checks in the path-like prefix.
//
// in total that gives us a decomposition of
//
//     (uri-scheme-like? (?! continues-as-uri) | _)
//     (path-segment-like? (?! continues-as-path))
//     id_rest*
struct identifier : _not_at_any_keyword<
    // we don't use (at<id_rest>, ...) matches here because identifiers are
    // a really hot path and rewinding as needed by at<> isn't entirely free.
    sor<
        seq<
            c::uri_scheme_first,
            star<ranges<'a', 'z', 'A', 'Z', '0', '9', '-'>>,
            not_at<_extend_as_uri>
        >,
        one<'_'>
    >,
    star<sor<ranges<'a', 'z', 'A', 'Z', '0', '9'>, one<'_', '-'>>>,
    not_at<_extend_as_path>,
    star<c::id_rest>
> {};

// floats may extend ints, thus these rules are very similar.
struct integer : seq<
    sor<
        seq<range<'1', '9'>, star<digit>, not_at<one<'.'>>>,
        seq<one<'0'>, not_at<one<'.'>, digit>, star<digit>>
    >,
    not_at<_extend_as_path>
> {};

struct floating : seq<
    sor<
        seq<range<'1', '9'>, star<digit>, one<'.'>, star<digit>>,
        seq<opt<one<'0'>>, one<'.'>, plus<digit>>
    >,
    opt<one<'E', 'e'>, opt<one<'+', '-'>>, plus<digit>>,
    not_at<_extend_as_path>
> {};

struct uri : seq<
    c::uri_scheme_first,
    star<c::uri_scheme_rest>,
    c::uri_sep,
    plus<c::uri_rest>
> {};

struct sep : sor<
    plus<one<' ', '\t', '\r', '\n'>>,
    seq<one<'#'>, star<not_one<'\r', '\n'>>>,
    seq<string<'/', '*'>, until<string<'*', '/'>>>
> {};

}



using seps = star<t::sep>;


// marker for semantic rules. not handling one of these in an action that cares about
// semantics is probably an error.
struct semantic {};


struct expr;

struct _string {
    template<typename... Inner>
    struct literal : semantic, seq<Inner...> {};
    struct cr_lf : semantic, seq<one<'\r'>, opt<one<'\n'>>> {};
    struct interpolation : semantic, seq<
        p::string<'$', '{'>, seps,
        must<expr>, seps,
        must<one<'}'>>
    > {};
    struct escape : semantic, must<any> {};
};
struct string : _string, seq<
    one<'"'>,
    star<
        sor<
            _string::literal<plus<not_one<'$', '"', '\\', '\r'>>>,
            _string::cr_lf,
            _string::interpolation,
            _string::literal<one<'$'>, opt<one<'$'>>>,
            seq<one<'\\'>, _string::escape>
        >
    >,
    must<one<'"'>>
> {};

struct _ind_string {
    template<bool Indented, typename... Inner>
    struct literal : semantic, seq<Inner...> {};
    struct interpolation : semantic, seq<
        p::string<'$', '{'>, seps,
        must<expr>, seps,
        must<one<'}'>>
    > {};
    struct escape : semantic, must<any> {};
};
struct ind_string : _ind_string, seq<
    TAO_PEGTL_STRING("''"),
    opt<star<one<' '>>, one<'\n'>>,
    star<
        sor<
            _ind_string::literal<
                true,
                plus<
                    sor<
                        not_one<'$', '\''>,
                        seq<one<'$'>, not_one<'{', '\''>>,
                        seq<one<'\''>, not_one<'\'', '$'>>
                    >
                >
            >,
            _ind_string::interpolation,
            _ind_string::literal<false, one<'$'>>,
            _ind_string::literal<false, one<'\''>, not_at<one<'\''>>>,
            seq<one<'\''>, _ind_string::literal<false, p::string<'\'', '\''>>>,
            seq<
                p::string<'\'', '\''>,
                sor<
                    _ind_string::literal<false, one<'$'>>,
                    seq<one<'\\'>, _ind_string::escape>
                >
            >
        >
    >,
    must<TAO_PEGTL_STRING("''")>
> {};

struct _path {
    // legacy lexer rules. extra l_ to avoid reserved c++ identifiers.
    struct _l_PATH : seq<star<c::path>, plus<c::path_sep, plus<c::path>>, opt<c::path_sep>> {};
    struct _l_PATH_SEG : seq<star<c::path>, c::path_sep> {};
    struct _l_HPATH : seq<one<'~'>, plus<c::path_sep, plus<c::path>>, opt<c::path_sep>> {};
    struct _l_HPATH_START : TAO_PEGTL_STRING("~/") {};
    struct _path_str : sor<_l_PATH, _l_PATH_SEG, plus<c::path>> {};
    // modern rules
    template<typename... Inner>
    struct literal : semantic, seq<Inner...> {};
    struct interpolation : semantic, seq<
        p::string<'$', '{'>, seps,
        must<expr>, seps,
        must<one<'}'>>
    > {};
    struct anchor : semantic, sor<
        _l_PATH,
        seq<_l_PATH_SEG, at<TAO_PEGTL_STRING("${")>>
    > {};
    struct home_anchor : semantic, sor<
        _l_HPATH,
        seq<_l_HPATH_START, at<TAO_PEGTL_STRING("${")>>
    > {};
    struct searched_path : semantic, list<plus<c::path>, c::path_sep> {};
    struct forbid_prefix_triple_slash : sor<not_at<c::path_sep>, failure> {};
    struct forbid_prefix_double_slash_no_interp : sor<
        not_at<c::path_sep, star<c::path>, not_at<TAO_PEGTL_STRING("${")>>,
        failure
    > {};
    // legacy parser rules
    struct _str_rest : seq<
        must<forbid_prefix_double_slash_no_interp>,
        opt<literal<_path_str>>,
        must<forbid_prefix_triple_slash>,
        star<
            sor<
                literal<_path_str>,
                interpolation
            >
        >
    > {};
};
struct path : _path, sor<
    seq<
        sor<_path::anchor, _path::home_anchor>,
        _path::_str_rest
    >,
    seq<one<'<'>, _path::searched_path, one<'>'>>
> {};

struct _formal {
    struct name : semantic, t::identifier {};
    struct default_value : semantic, must<expr> {};
};
struct formal : semantic, _formal, seq<
    _formal::name,
    opt<seps, one<'?'>, seps, _formal::default_value>
> {};

struct _formals {
    struct ellipsis : semantic, p::ellipsis {};
};
struct formals : semantic, _formals, seq<
    one<'{'>, seps,
    // formals and attrsets share a two-token head sequence ('{' <id>).
    // this rule unrolls the formals list a bit to provide better error messages than
    // "expected '='" at the first ',' if formals are incorrect.
    sor<
        one<'}'>,
        seq<_formals::ellipsis, seps, must<one<'}'>>>,
        seq<
            formal, seps,
            if_then_else<
                at<one<','>>,
                seq<
                    star<one<','>, seps, formal, seps>,
                    opt<one<','>, seps, opt<_formals::ellipsis, seps>>,
                    must<one<'}'>>
                >,
                one<'}'>
            >
        >
    >
> {};

struct _attr {
    struct simple : semantic, sor<t::identifier, t::kw_or> {};
    struct string : semantic, seq<grammar::string> {};
    struct expr : semantic, seq<
        TAO_PEGTL_STRING("${"), seps,
        must<grammar::expr>, seps,
        must<one<'}'>>
    > {};
};
struct attr : _attr, sor<
    _attr::simple,
    _attr::string,
    _attr::expr
> {};

struct attrpath : list<attr, one<'.'>, t::sep> {};

struct _inherit {
    struct from : semantic, must<expr> {};
    struct attrs : list<attr, seps> {};
};
struct inherit : _inherit, seq<
    t::kw_inherit, seps,
    opt<one<'('>, seps, _inherit::from, seps, must<one<')'>>, seps>,
    opt<_inherit::attrs, seps>,
    must<one<';'>>
> {};

struct _binding {
    struct path : semantic, attrpath {};
    struct equal : one<'='> {};
    struct value : semantic, must<expr> {};
};
struct binding : _binding, seq<
    _binding::path, seps,
    must<_binding::equal>, seps,
    _binding::value, seps,
    must<one<';'>>
> {};

struct bindings : opt<list<sor<inherit, binding>, seps>> {};

struct op {
    enum class kind {
        // NOTE non-associativity is *NOT* handled in the grammar structure.
        // handling it in the grammar itself instead of in semantic actions
        // slows down the parser significantly and makes the rules *much*
        // harder to read. maybe this will be different at some point when
        // ! does not sit between two binary precedence levels.
        nonAssoc,
        leftAssoc,
        rightAssoc,
        unary,
    };
    template<typename Rule, unsigned Precedence, kind Kind = kind::leftAssoc>
    struct _op : Rule {
        static constexpr unsigned precedence = Precedence;
        static constexpr op::kind kind = Kind;
    };

    struct unary_minus : _op<t::op_minus,           3, kind::unary> {};

    // treating this like a unary postfix operator is sketchy, but that's
    // the most reasonable way to implement the operator precedence set forth
    // by the language way back. it'd be much better if `.` and `?` had the same
    // precedence, but alas.
    struct has_attr   : _op<seq<one<'?'>, seps, must<attrpath>>, 4> {};

    struct concat     : _op<TAO_PEGTL_STRING("++"),  5, kind::rightAssoc> {};
    struct mul        : _op<one<'*'>,                6> {};
    struct div        : _op<t::op_div,               6> {};
    struct plus       : _op<one<'+'>,                7> {};
    struct minus      : _op<t::op_minus,             7> {};
    struct not_       : _op<one<'!'>,                8, kind::unary> {};
    struct update     : _op<TAO_PEGTL_STRING("//"),  9, kind::rightAssoc> {};
    struct less_eq    : _op<TAO_PEGTL_STRING("<="), 10, kind::nonAssoc> {};
    struct greater_eq : _op<TAO_PEGTL_STRING(">="), 10, kind::nonAssoc> {};
    struct less       : _op<one<'<'>,               10, kind::nonAssoc> {};
    struct greater    : _op<one<'>'>,               10, kind::nonAssoc> {};
    struct equals     : _op<TAO_PEGTL_STRING("=="), 11, kind::nonAssoc> {};
    struct not_equals : _op<TAO_PEGTL_STRING("!="), 11, kind::nonAssoc> {};
    struct and_       : _op<TAO_PEGTL_STRING("&&"), 12> {};
    struct or_        : _op<TAO_PEGTL_STRING("||"), 13> {};
    struct implies    : _op<TAO_PEGTL_STRING("->"), 14, kind::rightAssoc> {};
};

struct _expr {
    template<template<typename...> class OpenMod = seq, typename... Init>
    struct _attrset : seq<
        Init...,
        OpenMod<one<'{'>>, seps,
        bindings, seps,
        must<one<'}'>>
    > {};

    struct select;

    struct id : semantic, t::identifier {};
    struct int_ : semantic, t::integer {};
    struct float_ : semantic, t::floating {};
    struct string : semantic, seq<grammar::string> {};
    struct ind_string : semantic, seq<grammar::ind_string> {};
    struct path : semantic, seq<grammar::path> {};
    struct uri : semantic, t::uri {};
    struct ancient_let : semantic, _attrset<must, t::kw_let, seps> {};
    struct rec_set : semantic, _attrset<must, t::kw_rec, seps> {};
    struct set : semantic, _attrset<> {};

    struct _list {
        struct entry : semantic, seq<select> {};
    };
    struct list : semantic, _list, seq<
        one<'['>, seps,
        opt<p::list<_list::entry, seps>, seps>,
        must<one<']'>>
    > {};

    struct _simple : sor<
        id,
        int_,
        float_,
        string,
        ind_string,
        path,
        uri,
        seq<one<'('>, seps, must<expr>, seps, must<one<')'>>>,
        ancient_let,
        rec_set,
        set,
        list
    > {};

    struct _select {
        struct head : _simple {};
        struct attr : semantic, seq<attrpath> {};
        struct attr_or : semantic, must<select> {};
        struct as_app_or : semantic, t::kw_or {};
    };
    struct _app {
        struct first_arg : semantic, seq<select> {};
        struct another_arg : semantic, seq<select> {};
        // can be used to stash a position of the application head node
        struct select_or_fn : seq<select> {};
    };

    struct select : _select, seq<
        _select::head, seps,
        opt<
            sor<
                seq<
                    one<'.'>, seps, _select::attr,
                    opt<seps, t::kw_or, seps, _select::attr_or>
                >,
                _select::as_app_or
            >
        >
    > {};

    struct app : _app, seq<
        _app::select_or_fn,
        opt<seps, _app::first_arg, star<seps, _app::another_arg>>
    > {};

    template<typename Op>
    struct operator_ : semantic, Op {};

    struct unary : seq<
        star<sor<operator_<op::not_>, operator_<op::unary_minus>>, seps>,
        app
    > {};

    struct _binary_operator : sor<
        operator_<op::implies>,
        operator_<op::update>,
        operator_<op::concat>,
        operator_<op::plus>,
        operator_<op::minus>,
        operator_<op::mul>,
        operator_<op::div>,
        operator_<op::less_eq>,
        operator_<op::greater_eq>,
        operator_<op::less>,
        operator_<op::greater>,
        operator_<op::equals>,
        operator_<op::not_equals>,
        operator_<op::or_>,
        operator_<op::and_>
    > {};

    struct _binop : seq<
        unary,
        star<
            seps,
            sor<
                seq<_binary_operator, seps, must<unary>>,
                operator_<op::has_attr>
            >
        >
    > {};

    struct _lambda {
        struct arg : semantic, t::identifier {};
    };
    struct lambda : semantic, _lambda, sor<
        seq<
            _lambda::arg, seps,
            sor<
                seq<one<':'>, seps, must<expr>>,
                seq<one<'@'>, seps, must<formals, seps, one<':'>, seps, expr>>
            >
        >,
        seq<
            formals, seps,
            sor<
                seq<one<':'>, seps, must<expr>>,
                seq<one<'@'>, seps, must<_lambda::arg, seps, one<':'>, seps, expr>>
            >
        >
    > {};

    struct assert_ : semantic, seq<
        t::kw_assert, seps,
        must<expr>, seps,
        must<one<';'>>, seps,
        must<expr>
    > {};
    struct with : semantic, seq<
        t::kw_with, seps,
        must<expr>, seps,
        must<one<';'>>, seps,
        must<expr>
    > {};
    struct let : seq<
        t::kw_let, seps,
        not_at<one<'{'>>, // exclude ancient_let so we can must<kw_in>
        bindings, seps,
        must<t::kw_in>, seps,
        must<expr>
    > {};
    struct if_ : semantic, seq<
        t::kw_if, seps,
        must<expr>, seps,
        must<t::kw_then>, seps,
        must<expr>, seps,
        must<t::kw_else>, seps,
        must<expr>
    > {};
};
struct expr : semantic, _expr, sor<
    _expr::lambda,
    _expr::assert_,
    _expr::with,
    _expr::let,
    _expr::if_,
    _expr::_binop
> {};

// legacy support: \0 terminates input if passed from flex to bison as a token
struct eof : sor<p::eof, one<0>> {};

struct root : must<seps, expr, seps, eof> {};



template<typename Rule>
struct nothing : p::nothing<Rule> {
    static_assert(!std::is_base_of_v<semantic, Rule>);
};



template<typename Self, typename OpCtx, typename AttrPathT, typename ExprT>
struct operator_semantics {
    struct has_attr : grammar::op::has_attr {
        AttrPathT path;
    };

    struct OpEntry {
        OpCtx ctx;
        uint8_t prec;
        grammar::op::kind assoc;
        std::variant<
            grammar::op::not_,
            grammar::op::unary_minus,
            grammar::op::implies,
            grammar::op::or_,
            grammar::op::and_,
            grammar::op::equals,
            grammar::op::not_equals,
            grammar::op::less_eq,
            grammar::op::greater_eq,
            grammar::op::update,
            grammar::op::concat,
            grammar::op::less,
            grammar::op::greater,
            grammar::op::plus,
            grammar::op::minus,
            grammar::op::mul,
            grammar::op::div,
            has_attr
        > op;
    };

    // statistics here are taken from nixpkgs commit de502c4d0ba96261e5de803e4d1d1925afd3e22f.
    // over 99.9% of contexts in nixpkgs need at most 4 slots, ~85% need only 1
    boost::container::small_vector<ExprT, 4> exprs;
    // over 99.9% of contexts in nixpkgs need at most 2 slots, ~85% need only 1
    boost::container::small_vector<OpEntry, 2> ops;

    // derived class is expected to define members:
    //
    // ExprT applyOp(OpCtx & pos, auto & op, auto &... args);
    // [[noreturn]] static void badOperator(OpCtx & pos, auto &... args);

    void reduce(uint8_t toPrecedence, auto &... args) {
        while (!ops.empty()) {
            auto & [ctx, precedence, kind, op] = ops.back();
            // NOTE this relies on associativity not being mixed within a precedence level.
            if ((precedence > toPrecedence)
                || (kind != grammar::op::kind::leftAssoc && precedence == toPrecedence))
                break;
            std::visit([&, ctx=std::move(ctx)] (auto & op) {
                exprs.push_back(static_cast<Self &>(*this).applyOp(ctx, op, args...));
            }, op);
            ops.pop_back();
        }
    }

    ExprT popExpr()
    {
        auto r = std::move(exprs.back());
        exprs.pop_back();
        return r;
    }

    void pushOp(OpCtx ctx, auto o, auto &... args)
    {
        if (o.kind != grammar::op::kind::unary)
            reduce(o.precedence, args...);
        if (!ops.empty() && o.kind == grammar::op::kind::nonAssoc) {
            auto & [_pos, _prec, _kind, _o] = ops.back();
            if (_kind == o.kind && _prec == o.precedence)
                Self::badOperator(ctx, args...);
        }
        ops.emplace_back(ctx, o.precedence, o.kind, std::move(o));
    }

    ExprT finish(auto &... args)
    {
        reduce(255, args...);
        return popExpr();
    }
};

}
