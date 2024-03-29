#include "error.hh"
#include "environment-variables.hh"
#include "signals.hh"
#include "terminal.hh"
#include "position.hh"

#include <iostream>
#include <optional>
#include "serialise.hh"
#include <sstream>

namespace nix {

void BaseError::addTrace(std::shared_ptr<Pos> && e, HintFmt hint, TracePrint print)
{
    err.traces.push_front(Trace { .pos = std::move(e), .hint = hint, .print = print });
}

void throwExceptionSelfCheck()
{
    // This is meant to be caught in initLibUtil()
    throw Error("C++ exception handling is broken. This would appear to be a problem with the way Nix was compiled and/or linked and/or loaded.");
}

// c++ std::exception descendants must have a 'const char* what()' function.
// This stringifies the error and caches it for use by what(), or similarly by msg().
const std::string & BaseError::calcWhat() const
{
    if (what_.has_value())
        return *what_;
    else {
        std::ostringstream oss;
        showErrorInfo(oss, err, loggerSettings.showTrace);
        what_ = oss.str();
        return *what_;
    }
}

std::optional<std::string> ErrorInfo::programName = std::nullopt;

std::ostream & operator <<(std::ostream & os, const HintFmt & hf)
{
    return os << hf.str();
}

/**
 * An arbitrarily defined value comparison for the purpose of using traces in the key of a sorted container.
 */
inline bool operator<(const Trace& lhs, const Trace& rhs)
{
    // `std::shared_ptr` does not have value semantics for its comparison
    // functions, so we need to check for nulls and compare the dereferenced
    // values here.
    if (lhs.pos != rhs.pos) {
        if (!lhs.pos)
            return true;
        if (!rhs.pos)
            return false;
        if (*lhs.pos != *rhs.pos)
            return *lhs.pos < *rhs.pos;
    }
    // This formats a freshly formatted hint string and then throws it away, which
    // shouldn't be much of a problem because it only runs when pos is equal, and this function is
    // used for trace printing, which is infrequent.
    return lhs.hint.str() < rhs.hint.str();
}
inline bool operator> (const Trace& lhs, const Trace& rhs) { return rhs < lhs; }
inline bool operator<=(const Trace& lhs, const Trace& rhs) { return !(lhs > rhs); }
inline bool operator>=(const Trace& lhs, const Trace& rhs) { return !(lhs < rhs); }

// print lines of code to the ostream, indicating the error column.
void printCodeLines(std::ostream & out,
    const std::string & prefix,
    const Pos & errPos,
    const LinesOfCode & loc)
{
    // previous line of code.
    if (loc.prevLineOfCode.has_value()) {
        out << std::endl
            << fmt("%1% %|2$5d|| %3%",
            prefix,
            (errPos.line - 1),
            *loc.prevLineOfCode);
    }

    if (loc.errLineOfCode.has_value()) {
        // line of code containing the error.
        out << std::endl
            << fmt("%1% %|2$5d|| %3%",
            prefix,
            (errPos.line),
            *loc.errLineOfCode);
        // error arrows for the column range.
        if (errPos.column > 0) {
            int start = errPos.column;
            std::string spaces;
            for (int i = 0; i < start; ++i) {
                spaces.append(" ");
            }

            std::string arrows("^");

            out << std::endl
                << fmt("%1%      |%2%" ANSI_RED "%3%" ANSI_NORMAL,
                prefix,
                spaces,
                arrows);
        }
    }

    // next line of code.
    if (loc.nextLineOfCode.has_value()) {
        out << std::endl
            << fmt("%1% %|2$5d|| %3%",
            prefix,
            (errPos.line + 1),
            *loc.nextLineOfCode);
    }
}

static std::string indent(std::string_view indentFirst, std::string_view indentRest, std::string_view s)
{
    std::string res;
    bool first = true;

    while (!s.empty()) {
        auto end = s.find('\n');
        if (!first) res += "\n";
        res += chomp(std::string(first ? indentFirst : indentRest) + std::string(s.substr(0, end)));
        first = false;
        if (end == s.npos) break;
        s = s.substr(end + 1);
    }

    return res;
}

/**
 * A development aid for finding missing positions, to improve error messages. Example use:
 *
 *     _NIX_EVAL_SHOW_UNKNOWN_LOCATIONS=1 _NIX_TEST_ACCEPT=1 make tests/lang.sh.test
 *     git diff -U20 tests
 *
 */
static bool printUnknownLocations = getEnv("_NIX_EVAL_SHOW_UNKNOWN_LOCATIONS").has_value();

/**
 * Print a position, if it is known.
 *
 * @return true if a position was printed.
 */
static bool printPosMaybe(std::ostream & oss, std::string_view indent, const std::shared_ptr<Pos> & pos) {
    bool hasPos = pos && *pos;
    if (hasPos) {
        oss << indent << ANSI_BLUE << "at " ANSI_WARNING << *pos << ANSI_NORMAL << ":";

        if (auto loc = pos->getCodeLines()) {
            printCodeLines(oss, "", *pos, *loc);
            oss << "\n";
        }
    } else if (printUnknownLocations) {
        oss << "\n" << indent << ANSI_BLUE << "at " ANSI_RED << "UNKNOWN LOCATION" << ANSI_NORMAL << "\n";
    }
    return hasPos;
}

static void printTrace(
    std::ostream & output,
    const std::string_view & indent,
    size_t & count,
    const Trace & trace)
{
    output << "\n" << "… " << trace.hint.str() << "\n";

    if (printPosMaybe(output, indent, trace.pos))
        count++;
}

void printSkippedTracesMaybe(
    std::ostream & output,
    const std::string_view & indent,
    size_t & count,
    std::vector<Trace> & skippedTraces,
    std::set<Trace> tracesSeen)
{
    if (skippedTraces.size() > 0) {
        // If we only skipped a few frames, print them out normally;
        // messages like "1 duplicate frames omitted" aren't helpful.
        if (skippedTraces.size() <= 5) {
            for (auto & trace : skippedTraces) {
                printTrace(output, indent, count, trace);
            }
        } else {
            output << "\n" << ANSI_WARNING "(" << skippedTraces.size() << " duplicate frames omitted)" ANSI_NORMAL << "\n";
            // Clear the set of "seen" traces after printing a chunk of
            // `duplicate frames omitted`.
            //
            // Consider a mutually recursive stack trace with:
            // - 10 entries of A
            // - 10 entries of B
            // - 10 entries of A
            //
            // If we don't clear `tracesSeen` here, we would print output like this:
            // - 1 entry of A
            // - (9 duplicate frames omitted)
            // - 1 entry of B
            // - (19 duplicate frames omitted)
            //
            // This would obscure the control flow, which went from A,
            // to B, and back to A again.
            //
            // In contrast, if we do clear `tracesSeen`, the output looks like this:
            // - 1 entry of A
            // - (9 duplicate frames omitted)
            // - 1 entry of B
            // - (9 duplicate frames omitted)
            // - 1 entry of A
            // - (9 duplicate frames omitted)
            //
            // See: `tests/functional/lang/eval-fail-mutual-recursion.nix`
            tracesSeen.clear();
        }
    }
    // We've either printed each trace in `skippedTraces` normally, or
    // printed a chunk of `duplicate frames omitted`. Either way, we've
    // processed these traces and can clear them.
    skippedTraces.clear();
}

std::ostream & showErrorInfo(std::ostream & out, const ErrorInfo & einfo, bool showTrace)
{
    std::string prefix;
    switch (einfo.level) {
        case Verbosity::lvlError: {
            prefix = ANSI_RED "error";
            break;
        }
        case Verbosity::lvlNotice: {
            prefix = ANSI_RED "note";
            break;
        }
        case Verbosity::lvlWarn: {
            prefix = ANSI_WARNING "warning";
            break;
        }
        case Verbosity::lvlInfo: {
            prefix = ANSI_GREEN "info";
            break;
        }
        case Verbosity::lvlTalkative: {
            prefix = ANSI_GREEN "talk";
            break;
        }
        case Verbosity::lvlChatty: {
            prefix = ANSI_GREEN "chat";
            break;
        }
        case Verbosity::lvlVomit: {
            prefix = ANSI_GREEN "vomit";
            break;
        }
        case Verbosity::lvlDebug: {
            prefix = ANSI_WARNING "debug";
            break;
        }
        default:
            assert(false);
    }

    // FIXME: show the program name as part of the trace?
    if (einfo.programName && einfo.programName != ErrorInfo::programName)
        prefix += fmt(" [%s]:" ANSI_NORMAL " ", einfo.programName.value_or(""));
    else
        prefix += ":" ANSI_NORMAL " ";

    std::ostringstream oss;

    /*
     * Traces
     * ------
     *
     *  The semantics of traces is a bit weird. We have only one option to
     *  print them and to make them verbose (--show-trace). In the code they
     *  are always collected, but they are not printed by default. The code
     *  also collects more traces when the option is on. This means that there
     *  is no way to print the simplified traces at all.
     *
     *  I (layus) designed the code to attach positions to a restricted set of
     *  messages. This means that we have  a lot of traces with no position at
     *  all, including most of the base error messages. For example "type
     *  error: found a string while a set was expected" has no position, but
     *  will come with several traces detailing it's precise relation to the
     *  closest know position. This makes erroring without printing traces
     *  quite useless.
     *
     *  This is why I introduced the idea to always print a few traces on
     *  error. The number 3 is quite arbitrary, and was selected so as not to
     *  clutter the console on error. For the same reason, a trace with an
     *  error position takes more space, and counts as two traces towards the
     *  limit.
     *
     *  The rest is truncated, unless --show-trace is passed. This preserves
     *  the same bad semantics of --show-trace to both show the trace and
     *  augment it with new data. Not too sure what is the best course of
     *  action.
     *
     *  The issue is that it is fundamentally hard to provide a trace for a
     *  lazy language. The trace will only cover the current spine of the
     *  evaluation, missing things that have been evaluated before. For
     *  example, most type errors are hard to inspect because there is not
     *  trace for the faulty value. These errors should really print the faulty
     *  value itself.
     *
     *  In function calls, the --show-trace flag triggers extra traces for each
     *  function invocation. These work as scopes, allowing to follow the
     *  current spine of the evaluation graph. Without that flag, the error
     *  trace should restrict itself to a restricted prefix of that trace,
     *  until the first scope. If we ever get to such a precise error
     *  reporting, there would be no need to add an arbitrary limit here. We
     *  could always print the full trace, and it would just be small without
     *  the flag.
     *
     *  One idea I had is for XxxError.addTrace() to perform nothing if one
     *  scope has already been traced. Alternatively, we could stop here when
     *  we encounter such a scope instead of after an arbitrary number of
     *  traces. This however requires to augment traces with the notion of
     *  "scope".
     *
     *  This is particularly visible in code like evalAttrs(...) where we have
     *  to make a decision between the two following options.
     *
     *  ``` long traces
     *  inline void EvalState::evalAttrs(Env & env, Expr * e, Value & v, const Pos & pos, std::string_view errorCtx)
     *  {
     *      try {
     *          e->eval(*this, env, v);
     *          if (v.type() != nAttrs)
     *              error<TypeError>("expected a set but found %1%", v);
     *      } catch (Error & e) {
     *          e.addTrace(pos, errorCtx);
     *          throw;
     *      }
     *  }
     *  ```
     *
     *  ``` short traces
     *  inline void EvalState::evalAttrs(Env & env, Expr * e, Value & v, const Pos & pos, std::string_view errorCtx)
     *  {
     *      e->eval(*this, env, v);
     *      try {
     *          if (v.type() != nAttrs)
     *              error<TypeError>("expected a set but found %1%", v);
     *      } catch (Error & e) {
     *          e.addTrace(pos, errorCtx);
     *          throw;
     *      }
     *  }
     *  ```
     *
     *  The second example can be rewritten more concisely, but kept in this
     *  form to highlight the symmetry. The first option adds more information,
     *  because whatever caused an error down the line, in the generic eval
     *  function, will get annotated with the code location that uses and
     *  required it. The second option is less verbose, but does not provide
     *  any context at all as to where and why a failing value was required.
     *
     *  Scopes would fix that, by adding context only when --show-trace is
     *  passed, and keeping the trace terse otherwise.
     *
     */

    // Enough indent to align with with the `... `
    // prepended to each element of the trace
    auto ellipsisIndent = "  ";

    if (!einfo.traces.empty()) {
        // Stack traces seen since we last printed a chunk of `duplicate frames
        // omitted`.
        std::set<Trace> tracesSeen;
        // A consecutive sequence of stack traces that are all in `tracesSeen`.
        std::vector<Trace> skippedTraces;
        size_t count = 0;
        bool truncate = false;

        for (const auto & trace : einfo.traces) {
            if (trace.hint.str().empty()) continue;

            if (!showTrace && count > 3) {
                truncate = true;
            }

            if (!truncate || trace.print == TracePrint::Always) {

                if (tracesSeen.count(trace)) {
                    skippedTraces.push_back(trace);
                    continue;
                }

                tracesSeen.insert(trace);

                printSkippedTracesMaybe(oss, ellipsisIndent, count, skippedTraces, tracesSeen);

                count++;

                printTrace(oss, ellipsisIndent, count, trace);
            }
        }


        printSkippedTracesMaybe(oss, ellipsisIndent, count, skippedTraces, tracesSeen);

        if (truncate) {
            oss << "\n" << ANSI_WARNING "(stack trace truncated; use '--show-trace' to show the full, detailed trace)" ANSI_NORMAL << "\n";
        }

        oss << "\n" << prefix;
    }

    oss << einfo.msg << "\n";

    printPosMaybe(oss, "", einfo.pos);

    auto suggestions = einfo.suggestions.trim();
    if (!suggestions.suggestions.empty()) {
        oss << "Did you mean " <<
            suggestions.trim() <<
            "?" << std::endl;
    }

    out << indent(prefix, std::string(filterANSIEscapes(prefix, true).size(), ' '), chomp(oss.str()));

    return out;
}

}
