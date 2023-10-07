/**
 * diagnostic.hh - This file declares records that related to nix diagnostic
 *
 * Diagnostics are structures with a main message,
 * and optionally some additional information (body).
 *
 * For diagnostics with a body,
 * they may need a special overrided function to format the message.
 *
 */
#pragma once

#include <string>
#include <vector>

#include "error.hh"
#include "nixexpr.hh"

namespace nix {

/**
 * The base class for all diagnostics.
 * concret diagnostic types are defined in Diagnostic*.inc
 */
struct Diag
{
    /**
     * The location of some diagnostic, currently it is at the beginning of tokens
     */
    PosIdx loc;

    /**
     * Unique identifier for internal use.
     */
    enum Kind {
#define DIAG_MERGE(SNAME, CNAME, SEVERITY) DK_##CNAME,
#include "diagnostics/merge.inc"
    };

    Diag() = default;
    Diag(PosIdx loc)
        : loc(loc){};

    /**
     * Each diagnostic contains a severity field,
     * should be "Fatal", "Error" or "Warning"
     * this will affect the eval process.
     *
     * "Fatal"   -- non-recoverable while parsing.
     * "Error"   -- recoverable while parsing, but should not eval
     * "Warning" -- recoverable while parsing, and we can eval the AST
     * "Note"    -- some additional information about the error.
     */
    enum Severity { DS_Fatal, DS_Error, DS_Warning, DS_Note };

    [[nodiscard]] virtual Kind kind() const = 0;

    /**
     * \brief short name.
     * There might be a human readable short name that controls the diagnostic
     * For example, one may pass -Wno-dup-formal to suppress duplicated formals.
     * A special case for parsing errors, generated from bison
     * have the sname "bison"
     */
    [[nodiscard]] virtual std::string_view sname() const = 0;

    /** Get severity */
    [[nodiscard]] virtual Severity severity() const = 0;

    /**
     * Format printable diagnostic, with string interpolated
     * e.g. "invalid integer %1%" -> "invalid integer 'bar'"
     */
    [[nodiscard]] virtual std::string_view format() const = 0;

    virtual ~Diag() = default;

    static Verbosity getVerb(Severity s)
    {
        switch (s) {
        case DS_Error:
        case DS_Fatal:
            return lvlError;
        case DS_Warning:
            return lvlWarn;
        case DS_Note:
            return lvlNotice;
        }
    }

    [[nodiscard]] ErrorInfo getErrorInfo(const PosTable & positions) const
    {
        return ErrorInfo{.msg = std::string(format()), .errPos = positions[loc]};
    }

    using Notes = std::vector<std::shared_ptr<Diag>>;

    [[nodiscard]] virtual Notes getNotes() const
    {
        return {};
    }
};

struct DiagWithNotes : Diag
{
    Diag::Notes notes;
    [[nodiscard]] Diag::Notes getNotes() const override
    {
        return notes;
    }
};

#define DIAG_SIMPLE(SNAME, CNAME, SEVERITY, MESSAGE) \
    struct Diag##CNAME : Diag \
    { \
        std::string_view format() const override \
        { \
            return MESSAGE; \
        } \
        std::string_view sname() const override \
        { \
            return SNAME; \
        } \
        Severity severity() const override \
        { \
            return DS_##SEVERITY; \
        } \
        Kind kind() const override \
        { \
            return DK_##CNAME; \
        } \
        Diag##CNAME() = default; \
        Diag##CNAME(PosIdx pos) \
            : Diag(pos) \
        { \
        } \
    };
#include "diagnostics/kinds.inc"
#undef DIAG_SIMPLE

#define DIAG_BODY(SNAME, CNAME, SEVERITY, BODY) struct Diag##CNAME : Diag BODY;
#include "diagnostics/kinds.inc"
#undef DIAG_BODY

// Implement trivial functions except ::format
#define DIAG_BODY(SNAME, CNAME, SEVERITY, BODY) \
    inline std::string_view Diag##CNAME::sname() const \
    { \
        return SNAME; \
    } \
    inline Diag::Severity Diag##CNAME::severity() const \
    { \
        return DS_##SEVERITY; \
    } \
    inline Diag::Kind Diag##CNAME::kind() const \
    { \
        return DK_##CNAME; \
    }
#include "diagnostics/kinds.inc"
#undef DIAG_BODY

inline DiagInvalidInteger::DiagInvalidInteger(PosIdx loc, std::string text)
    : Diag(loc)
    , text(std::move(text))
{
    text = hintfmt("invalid integer '%1%'", text).str();
}

inline DiagInvalidFloat::DiagInvalidFloat(PosIdx loc, std::string text)
    : Diag(loc)
    , text(std::move(text))
{
    text = hintfmt("invalid float '%1%'", text).str();
}

inline std::string_view DiagInvalidInteger::format() const
{
    return text;
}

inline std::string_view DiagInvalidFloat::format() const
{
    return text;
}

inline std::string_view DiagBisonParse::format() const
{
    return err;
}
struct DiagnosticEngine
{
    std::vector<std::unique_ptr<Diag>> errors;
    std::vector<std::unique_ptr<Diag>> warnings;

    void add(std::unique_ptr<Diag> D)
    {
        switch (D->severity()) {
        case Diag::DS_Fatal:
        case Diag::DS_Error: {
            errors.emplace_back(std::move(D));
            break;
        }
        case Diag::DS_Warning: {
            warnings.emplace_back(std::move(D));
            break;
        }
        case Diag::DS_Note: {
            // todo: unreachble
            assert(0);
        }
        }
    }

    void checkRaise(const PosTable & positions) const
    {
        if (!errors.empty()) {
            const Diag * back = errors[0].get();
            throw ParseError(back->getErrorInfo(positions));
        }
    }
};

} // namespace nix
