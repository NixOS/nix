#pragma once
///@file

#include <compare>
#include <memory>
#include <sstream>
#include <variant>
#include <vector>
#include <string>

namespace nix {
/*
 * A DFA parser for literate test cases for CLIs.
 *
 * FIXME: implement merging of these, so you can auto update cases that have
 * comments.
 *
 * Format:
 * COMMENTARY
 * INDENT PROMPT COMMAND
 * INDENT OUTPUT
 *
 * e.g.
 * commentary commentary commentary
 *   nix-repl> :t 1
 *   an integer
 *
 * Yields:
 * Commentary "commentary commentary commentary"
 * Command ":t 1"
 * Output "an integer"
 *
 * Note: one Output line is generated for each line of the sources, because
 * this is effectively necessary to be able to align them in the future to
 * auto-update tests.
 */
class CLILiterateParser
{
public:

    enum class NodeKind {
        COMMENTARY,
        COMMAND,
        OUTPUT,
    };

    struct Node
    {
        NodeKind kind;
        std::string text;
        std::strong_ordering operator<=>(Node const &) const = default;

        static Node mkCommentary(std::string text)
        {
            return Node{.kind = NodeKind::COMMENTARY, .text = text};
        }

        static Node mkCommand(std::string text)
        {
            return Node{.kind = NodeKind::COMMAND, .text = text};
        }

        static Node mkOutput(std::string text)
        {
            return Node{.kind = NodeKind::OUTPUT, .text = text};
        }

        auto print() const -> std::string;
    };

    CLILiterateParser(std::string prompt, size_t indent = 2);

    auto syntax() const -> std::vector<Node> const &;

    /** Feeds a character into the parser */
    void feed(char c);

    /** Feeds a string into the parser */
    void feed(std::string_view s);

    /** Parses an input in a non-streaming fashion */
    static auto parse(std::string prompt, std::string_view const & input, size_t indent = 2) -> std::vector<Node>;

    /** Consumes a CLILiterateParser and gives you the syntax out of it */
    static auto intoSyntax(CLILiterateParser && inst) -> std::vector<Node>;

private:

    struct AccumulatingState
    {
        std::string lineAccumulator;
    };
    struct Indent
    {
        size_t pos = 0;
    };
    struct Commentary : public AccumulatingState
    {};
    struct Prompt : AccumulatingState
    {
        size_t pos = 0;
    };
    struct Command : public AccumulatingState
    {};
    struct OutputLine : public AccumulatingState
    {};

    using State = std::variant<Indent, Commentary, Prompt, Command, OutputLine>;
    State state_;

    constexpr static auto stateDebug(State const&) -> const char *;

    const std::string prompt_;
    const size_t indent_;

    /** Last line was output, so we consider a blank to be part of the output */
    bool lastWasOutput_;

    std::vector<Node> syntax_;

    void transition(State newState);
    void onNewline();
};

// Override gtest printing for lists of nodes
void PrintTo(std::vector<CLILiterateParser::Node> const & nodes, std::ostream * os);
};
