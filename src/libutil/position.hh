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

    struct Stdin {
        ref<std::string> source;
        bool operator==(const Stdin & rhs) const
        { return *source == *rhs.source; }
        bool operator!=(const Stdin & rhs) const
        { return *source != *rhs.source; }
        bool operator<(const Stdin & rhs) const
        { return *source < *rhs.source; }
    };
    struct String {
        ref<std::string> source;
        bool operator==(const String & rhs) const
        { return *source == *rhs.source; }
        bool operator!=(const String & rhs) const
        { return *source != *rhs.source; }
        bool operator<(const String & rhs) const
        { return *source < *rhs.source; }
    };

    typedef std::variant<std::monostate, Stdin, String, SourcePath> Origin;

    Origin origin = std::monostate();

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

    bool operator==(const Pos & rhs) const = default;
    bool operator!=(const Pos & rhs) const = default;
    bool operator<(const Pos & rhs) const;

    struct LinesIterator {
        using difference_type = size_t;
        using value_type = std::string_view;
        using reference = const std::string_view &;
        using pointer = const std::string_view *;
        using iterator_category = std::input_iterator_tag;

        LinesIterator(): pastEnd(true) {}
        explicit LinesIterator(std::string_view input): input(input), pastEnd(input.empty()) {
            if (!pastEnd)
                bump(true);
        }

        LinesIterator & operator++() {
            bump(false);
            return *this;
        }
        LinesIterator operator++(int) {
            auto result = *this;
            ++*this;
            return result;
        }

        reference operator*() const { return curLine; }
        pointer operator->() const { return &curLine; }

        bool operator!=(const LinesIterator & other) const {
            return !(*this == other);
        }
        bool operator==(const LinesIterator & other) const {
            return (pastEnd && other.pastEnd)
                || (std::forward_as_tuple(input.size(), input.data())
                    == std::forward_as_tuple(other.input.size(), other.input.data()));
        }

    private:
        std::string_view input, curLine;
        bool pastEnd = false;

        void bump(bool atFirst);
    };
};

std::ostream & operator<<(std::ostream & str, const Pos & pos);

}
