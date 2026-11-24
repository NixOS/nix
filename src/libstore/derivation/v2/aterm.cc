#include "nix/store/derivation/v2/aterm.hh"
#include "nix/store/derivation/traditional.hh"
#include "nix/store/content-address.hh"
#include "nix/store/store-dir-config.hh"
#include "nix/util/overloaded.hh"

#include "../aterm-combinators.hh"

#include <algorithm>

namespace nix::derivation::v2 {

using namespace aterm;

static constexpr std::string_view header = "DrvWithVersion(\"v2\",";

bool isV2(std::string_view s)
{
    return s.starts_with(header);
}

/* --------------------------------------------------------------------------
   Terms: the generic values of options
   -------------------------------------------------------------------------- */

static Term parseTerm(StringViewStream & str)
{
    switch (str.peek()) {
    case '"':
        return Term{parseString(str).toOwned()};
    case '[': {
        Term::List list;
        parseList(str, [&] { list.items.push_back(parseTerm(str)); });
        return Term{std::move(list)};
    }
    case '(': {
        Term::Tuple tuple;
        expect(str, '(');
        while (true) {
            tuple.items.push_back(parseTerm(str));
            auto c = str.get();
            if (c == ')')
                break;
            if (c != ',')
                throw FormatError("invalid tuple");
        }
        return Term{std::move(tuple)};
    }
    default:
        throw FormatError("expected a string, list, or tuple");
    }
}

static void printTerm(std::string & res, const Term & term)
{
    std::visit(
        overloaded{
            [&](const std::string & s) { printString(res, s); },
            [&](const Term::List & list) {
                printList(res, list.items, [&](const Term & item) { printTerm(res, item); });
            },
            [&](const Term::Tuple & tuple) {
                res += '(';
                bool first = true;
                for (auto & item : tuple.items) {
                    if (!first)
                        res += ',';
                    first = false;
                    printTerm(res, item);
                }
                res += ')';
            },
        },
        term.raw);
}

static std::string encode(const Term & term)
{
    std::string res;
    printTerm(res, term);
    return res;
}

/**
 * A list of terms whose order is canonical: sets and maps are in
 * ascending order of their elements' encodings, with no duplicates.
 * Sorting by encoding rather than by value is what makes the order a
 * property of the format alone.
 */
static Term sortedList(std::vector<Term> items)
{
    std::vector<std::pair<std::string, Term>> encoded;
    for (auto & item : items)
        encoded.emplace_back(encode(item), std::move(item));
    std::sort(encoded.begin(), encoded.end(), [](auto & a, auto & b) { return a.first < b.first; });
    Term::List list;
    for (auto & [_, item] : encoded)
        list.items.push_back(std::move(item));
    return Term{std::move(list)};
}

static const std::string & asString(const Term & term, std::string_view what)
{
    auto * s = std::get_if<std::string>(&term.raw);
    if (!s)
        throw FormatError("%s must be a string", what);
    return *s;
}

static const std::vector<Term> & asList(const Term & term, std::string_view what)
{
    auto * l = std::get_if<Term::List>(&term.raw);
    if (!l)
        throw FormatError("%s must be a list", what);
    return l->items;
}

static const std::vector<Term> & asTuple(const Term & term, size_t arity, std::string_view what)
{
    auto * t = std::get_if<Term::Tuple>(&term.raw);
    if (!t || t->items.size() != arity)
        throw FormatError("%s must be a tuple of %d", what, arity);
    return t->items;
}

/**
 * The elements of a list representing a set or map, checking that they
 * are in canonical order.
 */
static const std::vector<Term> & asSortedList(const Term & term, std::string_view what)
{
    auto & items = asList(term, what);
    for (size_t i = 1; i < items.size(); ++i) {
        auto prev = encode(items[i - 1]), cur = encode(items[i]);
        if (prev == cur)
            throw FormatError("duplicate element in %s", what);
        if (!(prev < cur))
            throw FormatError("%s is not in sorted order", what);
    }
    return items;
}

static Term boolTerm(bool b)
{
    return Term{std::string{b ? "true" : "false"}};
}

static bool asBool(const Term & term, std::string_view what)
{
    auto & s = asString(term, what);
    if (s == "true")
        return true;
    if (s == "false")
        return false;
    throw FormatError("%s must be \"true\" or \"false\"", what);
}

static Term naturalTerm(uint64_t n)
{
    return Term{std::to_string(n)};
}

static uint64_t asNatural(const Term & term, std::string_view what)
{
    auto & s = asString(term, what);
    if (s.empty() || !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })
        || (s.size() > 1 && s[0] == '0'))
        throw FormatError("%s must be a natural number without leading zeros", what);
    auto n = string2Int<uint64_t>(s);
    if (!n)
        throw FormatError("%s is out of range", what);
    return *n;
}

static Term optionalTerm(std::optional<Term> t)
{
    Term::List list;
    if (t)
        list.items.push_back(std::move(*t));
    return Term{std::move(list)};
}

static std::optional<Term> asOptional(const Term & term, std::string_view what)
{
    auto & items = asList(term, what);
    if (items.size() > 1)
        throw FormatError("%s must be a list of at most one element", what);
    if (items.empty())
        return std::nullopt;
    return items[0];
}

static Term stringsTerm(const StringSet & strings)
{
    std::vector<Term> items;
    for (auto & s : strings)
        items.push_back(Term{s});
    return sortedList(std::move(items));
}

static StringSet asStrings(const Term & term, std::string_view what)
{
    StringSet res;
    for (auto & item : asSortedList(term, what))
        res.insert(asString(item, what));
    return res;
}

static Term derivingPathTerm(const StoreDirConfig & store, const SingleDerivedPath & p)
{
    return Term{p.to_string(store)};
}

static SingleDerivedPath asDerivingPath(
    const StoreDirConfig & store, const Term & term, std::string_view what, const ExperimentalFeatureSettings & xp)
{
    auto & s = asString(term, what);
    auto p = SingleDerivedPath::parse(store, s, xp);
    if (p.to_string(store) != s)
        throw FormatError("deriving path '%s' in %s is not in canonical form", s, what);
    return p;
}

static Term referenceTerm(const StoreDirConfig & store, const Reference & ref)
{
    return std::visit(
        overloaded{
            [&](const OutputName & name) { return Term{Term::Tuple{{Term{"output"}, Term{name}}}}; },
            [&](const SingleDerivedPath & p) {
                return Term{Term::Tuple{{Term{"input"}, derivingPathTerm(store, p)}}};
            },
        },
        ref);
}

static Reference asReference(
    const StoreDirConfig & store, const Term & term, std::string_view what, const ExperimentalFeatureSettings & xp)
{
    auto & items = asTuple(term, 2, what);
    auto & tag = asString(items[0], what);
    if (tag == "output")
        return OutputName{asString(items[1], what)};
    if (tag == "input")
        return asDerivingPath(store, items[1], what, xp);
    throw FormatError("%s must be tagged \"input\" or \"output\", not '%s'", what, tag);
}

static Term referencesTerm(const StoreDirConfig & store, const std::set<Reference> & refs)
{
    std::vector<Term> items;
    for (auto & r : refs)
        items.push_back(referenceTerm(store, r));
    return sortedList(std::move(items));
}

static std::set<Reference> asReferences(
    const StoreDirConfig & store, const Term & term, std::string_view what, const ExperimentalFeatureSettings & xp)
{
    std::set<Reference> res;
    for (auto & item : asSortedList(term, what))
        res.insert(asReference(store, item, what, xp));
    return res;
}

/* --------------------------------------------------------------------------
   Options
   -------------------------------------------------------------------------- */

using RawOptions = std::map<std::string, Term, std::less<>>;

static RawOptions parseOptions(StringViewStream & str)
{
    RawOptions res;
    parseMap(
        str,
        res,
        [&] {
            auto name = parseUnquotedString(str).toOwned();
            auto colon = name.find(':');
            if (colon == 0 || colon == std::string::npos || colon + 1 == name.size())
                throw FormatError("option name '%s' is not of the form '<namespace>:<name>'", name);
            return name;
        },
        [&](const auto &) { return parseTerm(str); },
        [](const auto & name) { return fmt("option '%s'", name); });
    return res;
}

static void printOptions(std::string & res, const RawOptions & options)
{
    printMap(
        res,
        options,
        [&](const auto & name) { printUnquotedString(res, name); },
        [&](const auto &, const Term & value) { printTerm(res, value); });
}

/**
 * Decode one known option, rejecting it if it is written with its
 * default value, which would be a second encoding of the derivation.
 */
template<typename T>
static void decodeOption(T & field, const T & defaultValue, const std::string & name, auto decode)
{
    field = decode();
    if (field == defaultValue)
        throw FormatError("option '%s' must be omitted when it has its default value", name);
}

template<typename T>
static void encodeOption(RawOptions & res, std::string name, const T & field, const T & defaultValue, auto encode)
{
    if (field != defaultValue)
        res.emplace(std::move(name), encode());
}

static TopOptions
decodeTopOptions(const StoreDirConfig & store, RawOptions raw, const ExperimentalFeatureSettings & xp)
{
    const TopOptions d;
    TopOptions o;
    for (auto & [name, term] : raw) {
        auto & t = term;
        if (name == "nix:additionalSandboxProfile")
            decodeOption(o.additionalSandboxProfile, d.additionalSandboxProfile, name, [&] {
                return asString(t, name);
            });
        else if (name == "nix:allowLocalNetworking")
            decodeOption(o.allowLocalNetworking, d.allowLocalNetworking, name, [&] { return asBool(t, name); });
        else if (name == "nix:allowSubstitutes")
            decodeOption(o.allowSubstitutes, d.allowSubstitutes, name, [&] { return asBool(t, name); });
        else if (name == "nix:exportReferencesGraph")
            decodeOption(o.exportReferencesGraph, d.exportReferencesGraph, name, [&] {
                decltype(o.exportReferencesGraph) res;
                for (auto & entry : asSortedList(t, name)) {
                    auto & pair = asTuple(entry, 2, name);
                    std::set<SingleDerivedPath> paths;
                    for (auto & p : asSortedList(pair[1], name))
                        paths.insert(asDerivingPath(store, p, name, xp));
                    res.emplace(asString(pair[0], name), std::move(paths));
                }
                return res;
            });
        else if (name == "nix:impureEnvVars")
            decodeOption(o.impureEnvVars, d.impureEnvVars, name, [&] { return asStrings(t, name); });
        else if (name == "nix:impureHostDeps")
            decodeOption(o.impureHostDeps, d.impureHostDeps, name, [&] { return asStrings(t, name); });
        else if (name == "nix:noChroot")
            decodeOption(o.noChroot, d.noChroot, name, [&] { return asBool(t, name); });
        else if (name == "nix:preferLocalBuild")
            decodeOption(o.preferLocalBuild, d.preferLocalBuild, name, [&] { return asBool(t, name); });
        else if (name == "nix:requiredSystemFeatures")
            decodeOption(o.requiredSystemFeatures, d.requiredSystemFeatures, name, [&] { return asStrings(t, name); });
        else
            o.unknown.emplace(name, std::move(term));
    }
    return o;
}

static RawOptions encodeTopOptions(const StoreDirConfig & store, const TopOptions & o)
{
    const TopOptions d;
    RawOptions res{o.unknown.begin(), o.unknown.end()};
    encodeOption(res, "nix:additionalSandboxProfile", o.additionalSandboxProfile, d.additionalSandboxProfile, [&] {
        return Term{o.additionalSandboxProfile};
    });
    encodeOption(res, "nix:allowLocalNetworking", o.allowLocalNetworking, d.allowLocalNetworking, [&] {
        return boolTerm(o.allowLocalNetworking);
    });
    encodeOption(res, "nix:allowSubstitutes", o.allowSubstitutes, d.allowSubstitutes, [&] {
        return boolTerm(o.allowSubstitutes);
    });
    encodeOption(res, "nix:exportReferencesGraph", o.exportReferencesGraph, d.exportReferencesGraph, [&] {
        std::vector<Term> entries;
        for (auto & [fileName, paths] : o.exportReferencesGraph) {
            std::vector<Term> ps;
            for (auto & p : paths)
                ps.push_back(derivingPathTerm(store, p));
            entries.push_back(Term{Term::Tuple{{Term{fileName}, sortedList(std::move(ps))}}});
        }
        return sortedList(std::move(entries));
    });
    encodeOption(
        res, "nix:impureEnvVars", o.impureEnvVars, d.impureEnvVars, [&] { return stringsTerm(o.impureEnvVars); });
    encodeOption(
        res, "nix:impureHostDeps", o.impureHostDeps, d.impureHostDeps, [&] { return stringsTerm(o.impureHostDeps); });
    encodeOption(res, "nix:noChroot", o.noChroot, d.noChroot, [&] { return boolTerm(o.noChroot); });
    encodeOption(res, "nix:preferLocalBuild", o.preferLocalBuild, d.preferLocalBuild, [&] {
        return boolTerm(o.preferLocalBuild);
    });
    encodeOption(res, "nix:requiredSystemFeatures", o.requiredSystemFeatures, d.requiredSystemFeatures, [&] {
        return stringsTerm(o.requiredSystemFeatures);
    });
    return res;
}

static OutputOptions
decodeOutputOptions(const StoreDirConfig & store, RawOptions raw, const ExperimentalFeatureSettings & xp)
{
    const OutputOptions d;
    OutputOptions o;
    auto & c = o.checks;
    auto & dc = d.checks;
    for (auto & [name, term] : raw) {
        auto & t = term;
        auto optRefs = [&] -> std::optional<std::set<Reference>> {
            auto inner = asOptional(t, name);
            if (!inner)
                throw FormatError("option '%s' must be omitted when it has its default value", name);
            return asReferences(store, *inner, name, xp);
        };
        auto optNat = [&] -> std::optional<uint64_t> {
            auto inner = asOptional(t, name);
            if (!inner)
                throw FormatError("option '%s' must be omitted when it has its default value", name);
            return asNatural(*inner, name);
        };
        if (name == "nix:allowedReferences")
            c.allowedReferences = optRefs();
        else if (name == "nix:allowedRequisites")
            c.allowedRequisites = optRefs();
        else if (name == "nix:disallowedReferences")
            decodeOption(c.disallowedReferences, dc.disallowedReferences, name, [&] {
                return asReferences(store, t, name, xp);
            });
        else if (name == "nix:disallowedRequisites")
            decodeOption(c.disallowedRequisites, dc.disallowedRequisites, name, [&] {
                return asReferences(store, t, name, xp);
            });
        else if (name == "nix:ignoreSelfRefs")
            decodeOption(c.ignoreSelfRefs, dc.ignoreSelfRefs, name, [&] { return asBool(t, name); });
        else if (name == "nix:maxClosureSize")
            c.maxClosureSize = optNat();
        else if (name == "nix:maxSize")
            c.maxSize = optNat();
        else if (name == "nix:unsafeDiscardReferences")
            decodeOption(o.unsafeDiscardReferences, d.unsafeDiscardReferences, name, [&] { return asBool(t, name); });
        else
            o.unknown.emplace(name, std::move(term));
    }
    return o;
}

static RawOptions encodeOutputOptions(const StoreDirConfig & store, const OutputOptions & o)
{
    const OutputOptions d;
    auto & c = o.checks;
    auto & dc = d.checks;
    RawOptions res{o.unknown.begin(), o.unknown.end()};
    auto optRefs = [&](const std::optional<std::set<Reference>> & refs) {
        return optionalTerm(refs ? std::optional{referencesTerm(store, *refs)} : std::nullopt);
    };
    auto optNat = [&](const std::optional<uint64_t> & n) {
        return optionalTerm(n ? std::optional{naturalTerm(*n)} : std::nullopt);
    };
    encodeOption(res, "nix:allowedReferences", c.allowedReferences, dc.allowedReferences, [&] {
        return optRefs(c.allowedReferences);
    });
    encodeOption(res, "nix:allowedRequisites", c.allowedRequisites, dc.allowedRequisites, [&] {
        return optRefs(c.allowedRequisites);
    });
    encodeOption(res, "nix:disallowedReferences", c.disallowedReferences, dc.disallowedReferences, [&] {
        return referencesTerm(store, c.disallowedReferences);
    });
    encodeOption(res, "nix:disallowedRequisites", c.disallowedRequisites, dc.disallowedRequisites, [&] {
        return referencesTerm(store, c.disallowedRequisites);
    });
    encodeOption(
        res, "nix:ignoreSelfRefs", c.ignoreSelfRefs, dc.ignoreSelfRefs, [&] { return boolTerm(c.ignoreSelfRefs); });
    encodeOption(
        res, "nix:maxClosureSize", c.maxClosureSize, dc.maxClosureSize, [&] { return optNat(c.maxClosureSize); });
    encodeOption(res, "nix:maxSize", c.maxSize, dc.maxSize, [&] { return optNat(c.maxSize); });
    encodeOption(res, "nix:unsafeDiscardReferences", o.unsafeDiscardReferences, d.unsafeDiscardReferences, [&] {
        return boolTerm(o.unsafeDiscardReferences);
    });
    return res;
}

/* --------------------------------------------------------------------------
   Interpolated strings
   -------------------------------------------------------------------------- */

static Interpolated
parseInterpolated(const StoreDirConfig & store, StringViewStream & str, const ExperimentalFeatureSettings & xp)
{
    Interpolated res;
    bool lastWasLiteral = false;
    parseList(str, [&] {
        if (str.peek() == '"') {
            auto literal = parseString(str).toOwned();
            if (literal.empty())
                throw FormatError("empty literal in interpolated string");
            if (lastWasLiteral)
                throw FormatError("adjacent literals in interpolated string");
            res.append(std::move(literal));
            lastWasLiteral = true;
        } else {
            res.append(asReference(store, parseTerm(str), "interpolated string reference", xp));
            lastWasLiteral = false;
        }
    });
    return res;
}

static void printInterpolated(const StoreDirConfig & store, std::string & res, const Interpolated & s)
{
    printList(res, s.pieces(), [&](const Interpolated::Piece & piece) {
        std::visit(
            overloaded{
                [&](const std::string & literal) { printString(res, literal); },
                [&](const Reference & ref) { printTerm(res, referenceTerm(store, ref)); },
            },
            piece);
    });
}

/* --------------------------------------------------------------------------
   Outputs and inputs
   -------------------------------------------------------------------------- */

static std::pair<ContentAddressMethod, HashAlgorithm>
parseMethodAlgo(std::string_view hashAlgoStr, const ExperimentalFeatureSettings & xp)
{
    ContentAddressMethod method = ContentAddressMethod::parsePrefix(hashAlgoStr);
    if (method == ContentAddressMethod::Raw::Text)
        xp.require(Xp::DynamicDerivations, "text-hashed derivation output");
    return {std::move(method), parseHashAlgo(hashAlgoStr, xp)};
}

static std::string printMethodAlgo(const ContentAddressMethod & method, HashAlgorithm algo)
{
    return std::string{method.renderPrefix()} + std::string{printHashAlgo(algo)};
}

static DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode
parseNode(StringViewStream & str, const ExperimentalFeatureSettings & xp)
{
    DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode node;
    expect(str, '(');
    parseList(str, [&] {
        auto name = parseUnquotedString(str).toOwned();
        checkMonotonic(node.value, name, [&] { return fmt("output name '%s'", name); });
        node.value.insert(node.value.end(), std::move(name));
    });
    expect(str, ',');
    parseMap(
        str,
        node.childMap,
        [&] { return parseUnquotedString(str).toOwned(); },
        [&](const auto & name) {
            xp.require(Xp::DynamicDerivations, [&] { return fmt("dynamic output '%s'", name); });
            return parseNode(str, xp);
        },
        [](const auto & name) { return fmt("dynamic output '%s'", name); });
    expect(str, ')');
    if (node.value.empty() && node.childMap.empty())
        throw FormatError("input derivation node names no outputs");
    return node;
}

static void printNode(std::string & res, const DerivedPathMap<std::set<OutputName, std::less<>>>::ChildNode & node)
{
    res += '(';
    printUnquotedStrings(res, node.value);
    res += ',';
    printMap(
        res,
        node.childMap,
        [&](const auto & name) { printUnquotedString(res, name); },
        [&](const auto &, const auto & child) { printNode(res, child); });
    res += ')';
}

/**
 * Every input reference --- in a string, an output check, or the export
 * references graph --- must be one of the inputs, and every output
 * reference must name an output.
 */
static void checkReferences(const StoreDirConfig & store, const Derivation & drv)
{
    auto inputs = drv.inputs.toSet();
    auto check = [&](const Reference & ref) {
        std::visit(
            overloaded{
                [&](const OutputName & name) {
                    if (!drv.outputs.contains(name))
                        throw FormatError("reference to output '%s', which the derivation does not have", name);
                },
                [&](const SingleDerivedPath & p) {
                    if (!inputs.contains(p))
                        throw FormatError(
                            "reference to '%s', which is not one of the derivation's inputs", p.to_string(store));
                },
            },
            ref);
    };
    auto checkString = [&](const Interpolated & s) {
        for (auto & piece : s.pieces())
            if (auto * ref = std::get_if<Reference>(&piece))
                check(*ref);
    };
    checkString(drv.builder);
    for (auto & a : drv.args)
        checkString(a);
    for (auto & [_, var] : drv.env)
        checkString(var.value);
    for (auto & [_, paths] : drv.options.exportReferencesGraph)
        for (auto & p : paths)
            check(p);
    for (auto & [_, output] : drv.outputs) {
        auto & c = output.options.checks;
        for (auto * refs : {c.allowedReferences ? &*c.allowedReferences : nullptr,
                            c.allowedRequisites ? &*c.allowedRequisites : nullptr,
                            &c.disallowedReferences,
                            &c.disallowedRequisites})
            if (refs)
                for (auto & r : *refs)
                    check(r);
    }
}

/* --------------------------------------------------------------------------
   The derivation
   -------------------------------------------------------------------------- */

Parsed parse(
    const StoreDirConfig & store, std::string_view s, std::string_view name, const ExperimentalFeatureSettings & xp)
{
    StringViewStream str{s};
    expect(str, header);

    Parsed res;
    auto & drv = res.drv;
    drv.name = name;

    bool anyRecorded = false, anyUnrecorded = false;
    parseMap(
        str,
        drv.outputs,
        [&] { return parseUnquotedString(str).toOwned(); },
        [&](const auto & outputName) {
            auto pathS = parseStorePathString(str, defaultSupportWindowsStoreDir).toOwned();
            expect(str, ',');
            auto hashAlgo = parseUnquotedString(str).toOwned();
            expect(str, ',');
            auto hash = parseUnquotedString(str).toOwned();
            expect(str, ',');
            Output output{
                .raw = InputAddressed{},
                .options = decodeOutputOptions(store, parseOptions(str), xp),
            };
            if (hashAlgo.empty() && hash.empty()) {
                if (pathS.empty())
                    anyUnrecorded = true;
                else {
                    anyRecorded = true;
                    res.recordedOutputPaths.emplace(outputName, store.parseStorePathCanonical(pathS));
                }
            } else if (!pathS.empty()) {
                throw FormatError(
                    "output '%s' is not input-addressed, and so must not record an output path", outputName);
            } else if (hashAlgo.empty()) {
                throw FormatError("hash specified without hash algorithm");
            } else if (hash.empty()) {
                xp.require(Xp::CaDerivations);
                auto [method, algo] = parseMethodAlgo(hashAlgo, xp);
                output.raw = derivation::Output::CAFloating{.method = std::move(method), .hashAlgo = algo};
            } else if (hash == "impure") {
                xp.require(Xp::ImpureDerivations);
                auto [method, algo] = parseMethodAlgo(hashAlgo, xp);
                output.raw = derivation::Output::Impure{.method = std::move(method), .hashAlgo = algo};
            } else {
                throw FormatError(
                    "output '%s' is fixed content-addressed, which version 2 derivations do not yet support",
                    outputName);
            }
            return output;
        },
        [](const auto & outputName) { return fmt("output name '%s'", outputName); });
    if (anyRecorded && anyUnrecorded)
        throw FormatError("either all input-addressed output paths must be recorded, or none");

    expect(str, ',');
    parseMap(
        str,
        drv.inputs.drvs.map,
        [&] { return DerivationPath::require(parseStorePath(store, str, defaultSupportWindowsStoreDir)); },
        [&](const auto &) { return parseNode(str, xp); },
        [&](const auto & drvPath) { return fmt("input derivation '%s'", store.printStorePath(drvPath)); });

    expect(str, ',');
    drv.inputs.srcs = parseStorePaths(store, str, defaultSupportWindowsStoreDir);
    expect(str, ',');
    drv.platform = parseUnquotedString(str).toOwned();
    expect(str, ',');
    drv.builder = parseInterpolated(store, str, xp);
    expect(str, ',');
    parseList(str, [&] { drv.args.push_back(parseInterpolated(store, str, xp)); });
    expect(str, ',');
    parseMap(
        str,
        drv.env,
        [&] { return parseString(str).toOwned(); },
        [&](const auto &) {
            expect(str, '(');
            EnvVar var{.value = parseInterpolated(store, str, xp)};
            expect(str, ',');
            var.passAsFile = asBool(parseTerm(str), "pass as file flag");
            expect(str, ')');
            return var;
        },
        [](const auto & name) { return fmt("environment variable '%s'", name); });
    expect(str, ',');
    drv.options = decodeTopOptions(store, parseOptions(str), xp);
    expect(str, ')');
    if (!str.remaining.empty())
        throw FormatError("expected end of file, found '%s'", str.remaining);

    checkReferences(store, drv);
    return res;
}

std::string unparse(
    const Derivation & drv,
    const StoreDirConfig & store,
    const std::map<OutputName, StorePath, std::less<>> & outputPaths)
{
    std::string s;
    s += header;

    printMap(
        s,
        drv.outputs,
        [&](const auto & outputName) { printUnquotedString(s, outputName); },
        [&](const auto & outputName, const Output & output) {
            std::visit(
                overloaded{
                    [&](const InputAddressed &) {
                        auto i = outputPaths.find(outputName);
                        printStorePathString(
                            s, i == outputPaths.end() ? "" : store.printStorePath(i->second),
                            defaultSupportWindowsStoreDir);
                        s += ",\"\",\"\"";
                    },
                    [&](const derivation::Output::CAFloating & o) {
                        s += "\"\",";
                        printUnquotedString(s, printMethodAlgo(o.method, o.hashAlgo));
                        s += ",\"\"";
                    },
                    [&](const derivation::Output::Impure & o) {
                        s += "\"\",";
                        printUnquotedString(s, printMethodAlgo(o.method, o.hashAlgo));
                        s += ",\"impure\"";
                    },
                },
                output.raw);
            s += ',';
            printOptions(s, encodeOutputOptions(store, output.options));
        });

    s += ',';
    printMap(
        s,
        drv.inputs.drvs.map,
        [&](const auto & drvPath) {
            printStorePathString(s, store.printStorePath(drvPath), defaultSupportWindowsStoreDir);
        },
        [&](const auto &, const auto & node) { printNode(s, node); });

    s += ',';
    printList(s, drv.inputs.srcs, [&](const StorePath & p) {
        printStorePathString(s, store.printStorePath(p), defaultSupportWindowsStoreDir);
    });
    s += ',';
    printUnquotedString(s, drv.platform);
    s += ',';
    printInterpolated(store, s, drv.builder);
    s += ',';
    printList(s, drv.args, [&](const Interpolated & a) { printInterpolated(store, s, a); });
    s += ',';
    printMap(
        s,
        drv.env,
        [&](const auto & name) { printString(s, name); },
        [&](const auto &, const EnvVar & var) {
            s += '(';
            printInterpolated(store, s, var.value);
            s += ',';
            printTerm(s, boolTerm(var.passAsFile));
            s += ')';
        });
    s += ',';
    printOptions(s, encodeTopOptions(store, drv.options));
    s += ')';
    return s;
}

} // namespace nix::derivation::v2
