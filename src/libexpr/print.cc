#include <limits>
#include <sstream>

#include "nix/expr/print.hh"
#include "nix/util/ansicolor.hh"
#include "nix/util/signals.hh"
#include "nix/store/store-api.hh"
#include "nix/util/terminal.hh"
#include "nix/util/english.hh"
#include "nix/expr/eval.hh"

#include <boost/unordered/unordered_flat_set.hpp>

namespace nix {

void printElided(
    std::ostream & output,
    unsigned int value,
    const std::string_view single,
    const std::string_view plural,
    bool ansiColors)
{
    if (ansiColors)
        output << ANSI_FAINT;
    output << "«";
    pluralize(output, value, single, plural);
    output << " elided»";
    if (ansiColors)
        output << ANSI_NORMAL;
}

std::ostream & printLiteralString(std::ostream & str, const std::string_view string, size_t maxLength, bool ansiColors)
{
    size_t charsPrinted = 0;
    if (ansiColors)
        str << ANSI_MAGENTA;
    str << "\"";
    for (auto i = string.begin(); i != string.end(); ++i) {
        if (charsPrinted >= maxLength) {
            str << "\" ";
            printElided(str, string.length() - charsPrinted, "byte", "bytes", ansiColors);
            return str;
        }

        if (*i == '\"' || *i == '\\')
            str << "\\" << *i;
        else if (*i == '\n')
            str << "\\n";
        else if (*i == '\r')
            str << "\\r";
        else if (*i == '\t')
            str << "\\t";
        else if (*i == '$' && *(i + 1) == '{')
            str << "\\" << *i;
        else
            str << *i;
        charsPrinted++;
    }
    str << "\"";
    if (ansiColors)
        str << ANSI_NORMAL;
    return str;
}

std::ostream & printLiteralString(std::ostream & str, const std::string_view string)
{
    return printLiteralString(str, string, std::numeric_limits<size_t>::max(), false);
}

std::ostream & printLiteralBool(std::ostream & str, bool boolean)
{
    str << (boolean ? "true" : "false");
    return str;
}

// Returns `true' is a string is a reserved keyword which requires quotation
// when printing attribute set field names.
//
// This list should generally be kept in sync with `./lexer.l'.
// You can test if a keyword needs to be added by running:
//   $ nix eval --expr '{ <KEYWORD> = 1; }'
// For example `or' doesn't need to be quoted.
bool isReservedKeyword(const std::string_view str)
{
    static const boost::unordered_flat_set<std::string_view> reservedKeywords = {
        "if", "then", "else", "assert", "with", "let", "in", "rec", "inherit"};
    return reservedKeywords.contains(str);
}

std::ostream & printIdentifier(std::ostream & str, std::string_view s)
{
    if (s.empty())
        str << "\"\"";
    else if (isReservedKeyword(s))
        str << '"' << s << '"';
    else {
        char c = s[0];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
            printLiteralString(str, s);
            return str;
        }
        for (auto c : s)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '\''
                  || c == '-')) {
                printLiteralString(str, s);
                return str;
            }
        str << s;
    }
    return str;
}

static bool isVarName(std::string_view s)
{
    if (s.size() == 0)
        return false;
    if (isReservedKeyword(s))
        return false;
    char c = s[0];
    if ((c >= '0' && c <= '9') || c == '-' || c == '\'')
        return false;
    for (auto & i : s)
        if (!((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z') || (i >= '0' && i <= '9') || i == '_' || i == '-'
              || i == '\''))
            return false;
    return true;
}

std::ostream & printAttributeName(std::ostream & str, std::string_view name)
{
    if (isVarName(name))
        str << name;
    else
        printLiteralString(str, name);
    return str;
}

bool isImportantAttrName(const std::string & attrName)
{
    return attrName == "type" || attrName == "_type";
}

typedef std::pair<std::string, Value *> AttrPair;

struct ImportantFirstAttrNameCmp
{

    bool operator()(const AttrPair & lhs, const AttrPair & rhs) const
    {
        auto lhsIsImportant = isImportantAttrName(lhs.first);
        auto rhsIsImportant = isImportantAttrName(rhs.first);
        return std::forward_as_tuple(!lhsIsImportant, lhs.first) < std::forward_as_tuple(!rhsIsImportant, rhs.first);
    }
};

typedef std::set<const void *> ValuesSeen;
typedef std::vector<std::pair<std::string, Value *>> AttrVec;

class Printer
{
private:
    std::ostream & output;
    EvalState & state;
    PrintOptions options;
    std::optional<ValuesSeen> seen;
    size_t totalAttrsPrinted = 0;
    size_t totalListItemsPrinted = 0;
    std::string indent;

    void increaseIndent()
    {
        if (options.shouldPrettyPrint()) {
            indent.append(options.prettyIndent, ' ');
        }
    }

    void decreaseIndent()
    {
        if (options.shouldPrettyPrint()) {
            assert(indent.size() >= options.prettyIndent);
            indent.resize(indent.size() - options.prettyIndent);
        }
    }

    /**
     * Print a space (for separating items or attributes).
     *
     * If pretty-printing is enabled, a newline and the current `indent` is
     * printed instead.
     */
    void printSpace(bool prettyPrint)
    {
        if (prettyPrint) {
            output << "\n" << indent;
        } else {
            output << " ";
        }
    }

    void printRepeated()
    {
        if (options.ansiColors)
            output << ANSI_MAGENTA;
        output << "«repeated»";
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    void printNullptr()
    {
        if (options.ansiColors)
            output << ANSI_MAGENTA;
        output << "«nullptr»";
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    void printElided(unsigned int value, const std::string_view single, const std::string_view plural)
    {
        ::nix::printElided(output, value, single, plural, options.ansiColors);
    }

    void printInt(Value & v)
    {
        if (options.ansiColors)
            output << ANSI_CYAN;
        output << v.integer();
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    void printFloat(Value & v)
    {
        if (options.ansiColors)
            output << ANSI_CYAN;
        output << v.fpoint();
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    void printBool(Value & v)
    {
        if (options.ansiColors)
            output << ANSI_CYAN;
        printLiteralBool(output, v.boolean());
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    void printString(Value & v)
    {
        printLiteralString(output, v.string_view(), options.maxStringLength, options.ansiColors);
    }

    void printPath(Value & v)
    {
        if (options.ansiColors)
            output << ANSI_GREEN;
        output << v.path().to_string(); // !!! escaping?
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    void printNull()
    {
        if (options.ansiColors)
            output << ANSI_CYAN;
        output << "null";
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    void printDerivation(Value & v)
    {
        std::optional<StorePath> storePath;
        if (auto i = v.attrs()->get(state.s.drvPath)) {
            NixStringContext context;
            storePath =
                state.coerceToStorePath(i->pos, *i->value, context, "while evaluating the drvPath of a derivation");
        }

        /* This unfortunately breaks printing nested values because of
           how the pretty printer is used (when pretting printing and warning
           to same terminal / std stream). */
#if 0
        if (storePath && !storePath->isDerivation())
            warn(
                "drvPath attribute '%s' is not a valid store path to a derivation, this value not work properly",
                state.store->printStorePath(*storePath));
#endif

        if (options.ansiColors)
            output << ANSI_GREEN;
        output << "«derivation";
        if (storePath) {
            output << " " << state.store->printStorePath(*storePath);
        }
        output << "»";
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    /**
     * @note This may force items.
     */
    bool shouldPrettyPrintAttrs(AttrVec & v)
    {
        if (!options.shouldPrettyPrint() || v.empty()) {
            return false;
        }

        // Pretty-print attrsets with more than one item.
        if (v.size() > 1) {
            return true;
        }

        auto item = v[0].second;
        if (!item) {
            return true;
        }

        // It is ok to force the item(s) here, because they will be printed anyway.
        state.forceValue(*item, item->determinePos(noPos));

        // Pretty-print single-item attrsets only if they contain nested
        // structures.
        auto itemType = item->type();
        return itemType == nList || itemType == nAttrs || itemType == nThunk;
    }

    void printAttrs(Value & v, size_t depth)
    {
        if (seen && !seen->insert(v.attrs()).second) {
            printRepeated();
            return;
        }

        if (options.force && options.derivationPaths && state.isDerivation(v)) {
            printDerivation(v);
        } else if (depth < options.maxDepth) {
            increaseIndent();
            output << "{";

            AttrVec sorted;
            for (auto & i : *v.attrs())
                sorted.emplace_back(std::pair(state.symbols[i.name], i.value));

            if (options.maxAttrs == std::numeric_limits<size_t>::max())
                std::sort(sorted.begin(), sorted.end());
            else
                std::sort(sorted.begin(), sorted.end(), ImportantFirstAttrNameCmp());

            auto prettyPrint = shouldPrettyPrintAttrs(sorted);

            size_t currentAttrsPrinted = 0;

            for (auto & i : sorted) {
                printSpace(prettyPrint);

                if (totalAttrsPrinted >= options.maxAttrs) {
                    printElided(sorted.size() - currentAttrsPrinted, "attribute", "attributes");
                    break;
                }

                printAttributeName(output, i.first);
                output << " = ";
                print(*i.second, depth + 1);
                output << ";";
                totalAttrsPrinted++;
                currentAttrsPrinted++;
            }

            decreaseIndent();
            printSpace(prettyPrint);
            output << "}";
        } else {
            output << "{ ... }";
        }
    }

    /**
     * @note This may force items.
     */
    bool shouldPrettyPrintList(std::span<Value * const> list)
    {
        if (!options.shouldPrettyPrint() || list.empty()) {
            return false;
        }

        // Pretty-print lists with more than one item.
        if (list.size() > 1) {
            return true;
        }

        auto item = list[0];
        if (!item) {
            return true;
        }

        // It is ok to force the item(s) here, because they will be printed anyway.
        state.forceValue(*item, item->determinePos(noPos));

        // Pretty-print single-item lists only if they contain nested
        // structures.
        auto itemType = item->type();
        return itemType == nList || itemType == nAttrs || itemType == nThunk;
    }

    void printList(Value & v, size_t depth)
    {
        if (seen && v.listSize() && !seen->insert(&v).second) {
            printRepeated();
            return;
        }

        if (depth < options.maxDepth) {
            increaseIndent();
            output << "[";
            auto listItems = v.listView();
            auto prettyPrint = shouldPrettyPrintList(listItems.span());

            size_t currentListItemsPrinted = 0;

            for (auto elem : listItems) {
                printSpace(prettyPrint);

                if (totalListItemsPrinted >= options.maxListItems) {
                    printElided(listItems.size() - currentListItemsPrinted, "item", "items");
                    break;
                }

                if (elem) {
                    print(*elem, depth + 1);
                } else {
                    printNullptr();
                }
                totalListItemsPrinted++;
                currentListItemsPrinted++;
            }

            decreaseIndent();
            printSpace(prettyPrint);
            output << "]";
        } else {
            output << "[ ... ]";
        }
    }

    void printFunction(Value & v)
    {
        if (options.ansiColors)
            output << ANSI_BLUE;
        output << "«";

        if (v.isLambda()) {
            output << "lambda";
            if (v.lambda().fun) {
                if (v.lambda().fun->name) {
                    output << " " << state.symbols[v.lambda().fun->name];
                }

                std::ostringstream s;
                s << state.positions[v.lambda().fun->pos];
                output << " @ " << filterANSIEscapes(s.view());
            }
        } else if (v.isPrimOp()) {
            if (v.primOp())
                output << *v.primOp();
            else
                output << "primop";
        } else if (v.isPrimOpApp()) {
            output << "partially applied ";
            auto primOp = v.primOpAppPrimOp();
            if (primOp)
                output << *primOp;
            else
                output << "primop";
        } else {
            unreachable();
        }

        output << "»";
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    void printThunk(Value & v)
    {
        if (v.isBlackhole()) {
            // Although we know for sure that it's going to be an infinite recursion
            // when this value is accessed _in the current context_, it's likely
            // that the user will misinterpret a simpler «infinite recursion» output
            // as a definitive statement about the value, while in fact it may be
            // a valid value after `builtins.trace` and perhaps some other steps
            // have completed.
            if (options.ansiColors)
                output << ANSI_RED;
            output << "«potential infinite recursion»";
            if (options.ansiColors)
                output << ANSI_NORMAL;
        } else if (v.isThunk() || v.isApp()) {
            if (options.ansiColors)
                output << ANSI_MAGENTA;
            output << "«thunk»";
            if (options.ansiColors)
                output << ANSI_NORMAL;
        } else {
            unreachable();
        }
    }

    void printExternal(Value & v)
    {
        v.external()->print(output);
    }

    void printUnknown()
    {
        if (options.ansiColors)
            output << ANSI_RED;
        output << "«unknown»";
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    void printError_(Error & e)
    {
        if (options.ansiColors)
            output << ANSI_RED;
        output << "«error: " << filterANSIEscapes(e.info().msg.str(), true) << "»";
        if (options.ansiColors)
            output << ANSI_NORMAL;
    }

    void print(Value & v, size_t depth)
    {
        output.flush();
        checkInterrupt();

        try {
            if (options.force) {
                state.forceValue(v, v.determinePos(noPos));
            }

            switch (v.type()) {

            case nInt:
                printInt(v);
                break;

            case nFloat:
                printFloat(v);
                break;

            case nBool:
                printBool(v);
                break;

            case nString:
                printString(v);
                break;

            case nPath:
                printPath(v);
                break;

            case nNull:
                printNull();
                break;

            case nAttrs:
                printAttrs(v, depth);
                break;

            case nList:
                printList(v, depth);
                break;

            case nFunction:
                printFunction(v);
                break;

            case nThunk:
                printThunk(v);
                break;

            case nExternal:
                printExternal(v);
                break;

            default:
                printUnknown();
                break;
            }
        } catch (Error & e) {
            if (options.errors == ErrorPrintBehavior::Throw
                || (options.errors == ErrorPrintBehavior::ThrowTopLevel && depth == 0)) {
                throw;
            }
            printError_(e);
        }
    }

public:
    Printer(std::ostream & output, EvalState & state, PrintOptions options)
        : output(output)
        , state(state)
        , options(options)
    {
    }

    void print(Value & v)
    {
        totalAttrsPrinted = 0;
        totalListItemsPrinted = 0;
        indent.clear();

        if (options.trackRepeated) {
            seen.emplace();
        } else {
            seen.reset();
        }

        ValuesSeen seen;
        print(v, 0);
    }
};

void printValue(EvalState & state, std::ostream & output, Value & v, PrintOptions options)
{
    Printer(output, state, options).print(v);
}

std::ostream & operator<<(std::ostream & output, const ValuePrinter & printer)
{
    printValue(printer.state, output, printer.value, printer.options);
    return output;
}

template<>
HintFmt & HintFmt::operator%(const ValuePrinter & value)
{
    fmt % value;
    return *this;
}

} // namespace nix
