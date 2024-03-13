#include "cli-literate-parser.hh"
#include "libexpr/print.hh"
#include "debug-char.hh"
#include "types.hh"
#include "util.hh"
#include <iostream>
#include <memory>
#include <boost/algorithm/string/trim.hpp>

using namespace std::string_literals;

namespace nix {

static constexpr const bool DEBUG_PARSER = false;

constexpr auto CLILiterateParser::stateDebug(State const & s) -> const char *
{
    return std::visit(
        overloaded{// clang-format off
            [](Indent const&) -> const char * { return "indent"; },
            [](Commentary const&) -> const char * { return "indent"; },
            [](Prompt const&) -> const char * { return "prompt"; },
            [](Command const&) -> const char * { return "command"; },
            [](OutputLine const&) -> const char * { return "output_line"; }},
        // clang-format on
        s);
}

auto CLILiterateParser::Node::print() const -> std::string
{
    std::ostringstream s{};
    switch (kind) {
    case NodeKind::COMMENTARY:
        s << "Commentary ";
        break;
    case NodeKind::COMMAND:
        s << "Command ";
        break;
    case NodeKind::OUTPUT:
        s << "Output ";
        break;
    }
    printLiteralString(s, this->text);
    return s.str();
}

void PrintTo(std::vector<CLILiterateParser::Node> const & nodes, std::ostream * os)
{
    for (auto & node : nodes) {
        *os << node.print() << "\\n";
    }
}

auto CLILiterateParser::parse(std::string prompt, std::string_view const & input, size_t indent) -> std::vector<Node>
{
    CLILiterateParser p{std::move(prompt), indent};
    p.feed(input);
    return CLILiterateParser::intoSyntax(std::move(p));
}

auto CLILiterateParser::intoSyntax(CLILiterateParser && inst) -> std::vector<Node>
{
    return std::move(inst.syntax_);
}

CLILiterateParser::CLILiterateParser(std::string prompt, size_t indent)
    : state_(indent == 0 ? State(Prompt{}) : State(Indent{}))
    , prompt_(prompt)
    , indent_(indent)
    , lastWasOutput_(false)
    , syntax_(std::vector<Node>{})
{
    assert(!prompt.empty());
}

void CLILiterateParser::feed(char c)
{
    if constexpr (DEBUG_PARSER) {
        std::cout << stateDebug(state_) << " " << DebugChar{c} << "\n";
    }

    if (c == '\n') {
        onNewline();
        return;
    }

    std::visit(
        overloaded{
            [&](Indent & s) {
                if (c == ' ') {
                    if (++s.pos >= indent_) {
                        transition(Prompt{});
                    }
                } else {
                    transition(Commentary{AccumulatingState{.lineAccumulator = std::string{c}}});
                }
            },
            [&](Prompt & s) {
                if (s.pos >= prompt_.length()) {
                    transition(Command{AccumulatingState{.lineAccumulator = std::string{c}}});
                    return;
                } else if (c == prompt_[s.pos]) {
                    // good prompt character
                    ++s.pos;
                } else {
                    // didn't match the prompt, so it must have actually been output.
                    s.lineAccumulator.push_back(c);
                    transition(OutputLine{AccumulatingState{.lineAccumulator = std::move(s.lineAccumulator)}});
                    return;
                }
                s.lineAccumulator.push_back(c);
            },
            [&](AccumulatingState & s) { s.lineAccumulator.push_back(c); }},
        state_);
}

void CLILiterateParser::onNewline()
{
    State lastState = std::move(state_);
    bool newLastWasOutput = false;

    syntax_.push_back(std::visit(
        overloaded{
            [&](Indent & s) {
                // XXX: technically this eats trailing spaces

                // a newline following output is considered part of that output
                if (lastWasOutput_) {
                    newLastWasOutput = true;
                    return Node::mkOutput("");
                }
                return Node::mkCommentary("");
            },
            [&](Commentary & s) { return Node::mkCommentary(std::move(s.lineAccumulator)); },
            [&](Command & s) { return Node::mkCommand(std::move(s.lineAccumulator)); },
            [&](OutputLine & s) {
                newLastWasOutput = true;
                return Node::mkOutput(std::move(s.lineAccumulator));
            },
            [&](Prompt & s) {
                // INDENT followed by newline is also considered a blank output line
                return Node::mkOutput(std::move(s.lineAccumulator));
            }},
        lastState));

    transition(Indent{});
    lastWasOutput_ = newLastWasOutput;
}

void CLILiterateParser::feed(std::string_view s)
{
    for (char ch : s) {
        feed(ch);
    }
}

void CLILiterateParser::transition(State new_state)
{
    // When we expect INDENT and we are parsing without indents, commentary
    // cannot exist, so we want to transition directly into PROMPT before
    // resuming normal processing.
    if (Indent * i = std::get_if<Indent>(&new_state); i != nullptr && indent_ == 0) {
        new_state = Prompt{AccumulatingState{}, i->pos};
    }

    state_ = new_state;
}

auto CLILiterateParser::syntax() const -> std::vector<Node> const &
{
    return syntax_;
}

};
