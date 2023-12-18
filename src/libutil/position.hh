#pragma once
/**
 * @file
 *
 * @brief Pos and AbstractPos
 */

#include <cstdint>
#include <string>

#include "source-path.hh"

namespace nix {

/**
 * A position and an origin for that position (like a source file).
 */
struct Pos
{
    uint32_t line = 0;
    uint32_t column = 0;

    struct none_tag {
        bool operator==(const none_tag & rhs) const
        { return true; }
    };
    struct Stdin {
        ref<std::string> source;
        bool operator==(const Stdin & rhs) const
        { return this->source == rhs.source; }
    };
    struct String {
        ref<std::string> source;
        bool operator==(const String & rhs) const
        { return this->source == rhs.source; }
    };

    typedef std::variant<none_tag, Stdin, String, SourcePath> Origin;

    Origin origin = none_tag();

    Pos() { }
    Pos(uint32_t line, uint32_t column, Origin origin)
        : line(line), column(column), origin(origin) { }
    Pos(Pos & other) = default;
    Pos(const Pos & other) = default;
    Pos(Pos && other) = default;
    Pos(const Pos * other);

    explicit operator bool() const { return line > 0; }

    operator std::shared_ptr<Pos>() const;

    /**
     * Return the contents of the source file.
     */
    std::optional<std::string> getSource() const;

    void print(std::ostream & out, bool showOrigin) const;

    std::optional<LinesOfCode> getCodeLines() const;
};

std::ostream & operator<<(std::ostream & str, const Pos & pos);

}
