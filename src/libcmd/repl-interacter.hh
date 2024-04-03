#pragma once
/// @file

#include "finally.hh"
#include "types.hh"
#include <functional>
#include <string>

namespace nix {

namespace detail {
/** Provides the completion hooks for the repl, without exposing its complete
 * internals. */
struct ReplCompleterMixin {
    virtual StringSet completePrefix(const std::string & prefix) = 0;
};
};

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
    virtual ~ReplInteracter(){};
};

class ReadlineLikeInteracter : public virtual ReplInteracter
{
    std::string historyFile;
public:
    ReadlineLikeInteracter(std::string historyFile)
        : historyFile(historyFile)
    {
    }
    virtual Guard init(detail::ReplCompleterMixin * repl) override;
    virtual bool getLine(std::string & input, ReplPromptType promptType) override;
    virtual ~ReadlineLikeInteracter() override;
};

};
