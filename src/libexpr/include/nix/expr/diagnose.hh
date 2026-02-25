#pragma once
///@file

#include <optional>

#include "nix/util/ansicolor.hh"
#include "nix/util/configuration.hh"
#include "nix/util/error.hh"
#include "nix/util/logging.hh"

namespace nix {

/**
 * Diagnostic level for deprecated or non-portable language features.
 */
enum struct Diagnose {
    /**
     * Ignore the feature without any diagnostic.
     */
    Ignore,
    /**
     * Warn when the feature is used, but allow it.
     */
    Warn,
    /**
     * Treat the feature as a fatal error.
     */
    Fatal,
};

template<>
Diagnose BaseSetting<Diagnose>::parse(const std::string & str) const;

template<>
std::string BaseSetting<Diagnose>::to_string() const;

/**
 * Check a diagnostic setting and either do nothing, log a warning, or throw an error.
 *
 * The setting name is automatically appended to the error message.
 *
 * @param setting The diagnostic setting to check
 * @param mkError A function that takes a bool (true if fatal, false if warning) and
 *                returns an optional error to throw (or warn with).
 *                Only called if level is not `Ignore`.
 *                If the function returns `std::nullopt`, no diagnostic is emitted.
 *
 * @throws The error returned by mkError if level is `Fatal` and mkError returns a value
 */
template<typename F>
void diagnose(const Setting<Diagnose> & setting, F && mkError)
{
    auto withError = [&](bool fatal, auto && handler) {
        auto maybeError = mkError(fatal);
        if (!maybeError)
            return;
        auto & info = maybeError->unsafeInfo();
        // Append the setting name to help users find the right setting
        info.msg = HintFmt("%s (" ANSI_BOLD "%s" ANSI_NORMAL ")", Uncolored(info.msg.str()), setting.name);
        maybeError->recalcWhat();
        handler(std::move(*maybeError));
    };

    switch (setting.get()) {
    case Diagnose::Ignore:
        return;
    case Diagnose::Warn:
        withError(false, [](auto && error) { logWarning(error.info()); });
        return;
    case Diagnose::Fatal:
        withError(true, [](auto && error) { throw std::move(error); });
        return;
    }
}

} // namespace nix
