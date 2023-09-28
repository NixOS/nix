/// diagnostic.hh - This file declares records that related to nix diagnostic
#pragma once

#include <string>
#include <vector>

#include "nixexpr.hh"

namespace nix {

/// The diagnostic
struct Diag
{
    PosIdx begin;
    /// Which diagnostic
    enum ID {
#define NIX_DIAG_ID(ID) ID,
#include "diagnostics-id.inc.hh"
#undef NIX_DIAG_ID
    };

    /// Tags
    enum Tag {
        DT_None,
        DT_Unnecessary,
        DT_Deprecated,
    };

    Diag(PosIdx begin)
        : begin(begin)
    {
    }

    enum Severity { DL_Warning, DL_Error, DL_Note };

    /// Additional information attached to some diagnostic.
    /// elaborting the problem,
    /// usaually points to a related piece of code.
    std::vector<Diag> notes;

    virtual Tag getTags() = 0;
    virtual ID getID() = 0;
    virtual Severity getServerity() = 0;
    [[nodiscard]] virtual std::string format() const = 0;

    virtual ~Diag() = default;
};

struct DiagnosticEngine
{
    std::vector<std::unique_ptr<Diag>> Errors;
    std::vector<std::unique_ptr<Diag>> Warnings;

    void add(std::unique_ptr<Diag> D)
    {
        // Currently we just make the severity as-is
        // we can use some flags to control (e.g. -Werror)
        if (D->getServerity() == Diag::DL_Error) {
            Errors.emplace_back(std::move(D));
        } else {
            Warnings.emplace_back(std::move(D));
        }
    }

    void checkRaise(const PosTable & positions) const
    {
        if (!Errors.empty()) {
            const Diag * back = Errors.back().get();
            throw ParseError(ErrorInfo{.msg = back->format(), .errPos = positions[back->begin]});
        }
    }
};

} // namespace nix
