#pragma once
/// @file

#include "nix/util/finally.hh"
#include "nix/util/types.hh"
#include <filesystem>
#include <functional>
#include <string>

namespace nix {

namespace detail {
/** Provides the completion hooks for the repl, without exposing its complete
 * internals. */
struct ReplCompleterMixin
{
    virtual StringSet completePrefix(const std::string & prefix) = 0;
    virtual ~ReplCompleterMixin() = default;
};
}; // namespace detail

enum class ReplPromptType {
    ReplPrompt,
    ContinuationPrompt,
};

class ReplInteracter
{
public:
    using Guard = Finally<std::function<void()>>;

    virtual Guard init(detail::ReplCompleterMixin * repl) = 0;
    /** Returns a boolean of whether the interacter got EOF */
    virtual bool getLine(std::string & input, ReplPromptType promptType) = 0;
    virtual ~ReplInteracter() {};
};

class ReadlineLikeInteracter : public virtual ReplInteracter
{
    std::filesystem::path historyFile;
public:
    ReadlineLikeInteracter(std::filesystem::path historyFile)
        : historyFile(std::move(historyFile))
    {
    }

    virtual Guard init(detail::ReplCompleterMixin * repl) override;
    virtual bool getLine(std::string & input, ReplPromptType promptType) override;
    virtual ~ReadlineLikeInteracter() override;
};

}; // namespace nix
