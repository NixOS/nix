#include "error.hh"

#include <iostream>
#include <optional>
#include "serialise.hh"
#include <sstream>

namespace nix {

const std::string nativeSystem = SYSTEM;

void BaseError::addTrace(std::shared_ptr<AbstractPos> && e, hintformat hint, bool frame)
{
    err.traces.push_front(Trace { .pos = std::move(e), .hint = hint, .frame = frame });
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

std::ostream & operator <<(std::ostream & os, const hintformat & hf)
{
    return os << hf.str();
}

std::ostream & operator <<(std::ostream & str, const AbstractPos & pos)
{
    pos.print(str);
    str << ":" << pos.line;
    if (pos.column > 0)
        str << ":" << pos.column;
    return str;
}

std::optional<LinesOfCode> AbstractPos::getCodeLines() const
{
    if (line == 0)
        return std::nullopt;

    if (auto source = getSource()) {

        std::istringstream iss(*source);
        // count the newlines.
        int count = 0;
        std::string curLine;
        int pl = line - 1;

        LinesOfCode loc;

        do {
            std::getline(iss, curLine);
            ++count;
            if (count < pl)
                ;
            else if (count == pl) {
                loc.prevLineOfCode = curLine;
            } else if (count == pl + 1) {
                loc.errLineOfCode = curLine;
            } else if (count == pl + 2) {
                loc.nextLineOfCode = curLine;
                break;
            }

            if (!iss.good())
                break;
        } while (true);

        return loc;
    }

    return std::nullopt;
}

// print lines of code to the ostream, indicating the error column.
void printCodeLines(std::ostream & out,
    const std::string & prefix,
    const AbstractPos & errPos,
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

    auto noSource = ANSI_ITALIC " (source not available)" ANSI_NORMAL "\n";

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
     *              throwTypeError("value is %1% while a set was expected", v);
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
     *              throwTypeError("value is %1% while a set was expected", v);
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

    bool frameOnly = false;
    if (!einfo.traces.empty()) {
        size_t count = 0;
        for (const auto & trace : einfo.traces) {
            if (!showTrace && count > 3) {
                oss << "\n" << ANSI_WARNING "(stack trace truncated; use '--show-trace' to show the full trace)" ANSI_NORMAL << "\n";
                break;
            }

            if (trace.hint.str().empty()) continue;
            if (frameOnly && !trace.frame) continue;

            count++;
            frameOnly = trace.frame;

            oss << "\n" << "… " << trace.hint.str() << "\n";

            if (trace.pos) {
                count++;

                oss << "\n" << ellipsisIndent << ANSI_BLUE << "at " ANSI_WARNING << *trace.pos << ANSI_NORMAL << ":";

                if (auto loc = trace.pos->getCodeLines()) {
                    oss << "\n";
                    printCodeLines(oss, "", *trace.pos, *loc);
                    oss << "\n";
                } else
                    oss << noSource;
            }
        }
        oss << "\n" << prefix;
    }

    oss << einfo.msg << "\n";

    if (einfo.errPos) {
        oss << "\n" << ANSI_BLUE << "at " ANSI_WARNING << *einfo.errPos << ANSI_NORMAL << ":";

        if (auto loc = einfo.errPos->getCodeLines()) {
            oss << "\n";
            printCodeLines(oss, "", *einfo.errPos, *loc);
            oss << "\n";
        } else
            oss << noSource;
    }

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
