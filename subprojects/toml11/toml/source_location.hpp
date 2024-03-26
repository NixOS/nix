//     Copyright Toru Niina 2019.
// Distributed under the MIT License.
#ifndef TOML11_SOURCE_LOCATION_HPP
#define TOML11_SOURCE_LOCATION_HPP
#include <cstdint>
#include <sstream>

#include "region.hpp"

namespace toml
{

// A struct to contain location in a toml file.
// The interface imitates std::experimental::source_location,
// but not completely the same.
//
// It would be constructed by toml::value. It can be used to generate
// user-defined error messages.
//
// - std::uint_least32_t line() const noexcept
//   - returns the line number where the region is on.
// - std::uint_least32_t column() const noexcept
//   - returns the column number where the region starts.
// - std::uint_least32_t region() const noexcept
//   - returns the size of the region.
//
// +-- line()       +-- region of interest (region() == 9)
// v            .---+---.
// 12 | value = "foo bar"
//              ^
//              +-- column()
//
// - std::string const& file_name() const noexcept;
//   - name of the file.
// - std::string const& line_str() const noexcept;
//   - the whole line that contains the region of interest.
//
struct source_location
{
  public:

    source_location()
        : line_num_(1), column_num_(1), region_size_(1),
          file_name_("unknown file"), line_str_("")
    {}

    explicit source_location(const detail::region_base* reg)
        : line_num_(1), column_num_(1), region_size_(1),
          file_name_("unknown file"), line_str_("")
    {
        if(reg)
        {
            if(reg->line_num() != detail::region_base().line_num())
            {
                line_num_ = static_cast<std::uint_least32_t>(
                        std::stoul(reg->line_num()));
            }
            column_num_  = static_cast<std::uint_least32_t>(reg->before() + 1);
            region_size_ = static_cast<std::uint_least32_t>(reg->size());
            file_name_   = reg->name();
            line_str_    = reg->line();
        }
    }

    explicit source_location(const detail::region& reg)
        : line_num_(static_cast<std::uint_least32_t>(std::stoul(reg.line_num()))),
          column_num_(static_cast<std::uint_least32_t>(reg.before() + 1)),
          region_size_(static_cast<std::uint_least32_t>(reg.size())),
          file_name_(reg.name()),
          line_str_ (reg.line())
    {}
    explicit source_location(const detail::location& loc)
        : line_num_(static_cast<std::uint_least32_t>(std::stoul(loc.line_num()))),
          column_num_(static_cast<std::uint_least32_t>(loc.before() + 1)),
          region_size_(static_cast<std::uint_least32_t>(loc.size())),
          file_name_(loc.name()),
          line_str_ (loc.line())
    {}

    ~source_location() = default;
    source_location(source_location const&) = default;
    source_location(source_location &&)     = default;
    source_location& operator=(source_location const&) = default;
    source_location& operator=(source_location &&)     = default;

    std::uint_least32_t line()      const noexcept {return line_num_;}
    std::uint_least32_t column()    const noexcept {return column_num_;}
    std::uint_least32_t region()    const noexcept {return region_size_;}

    std::string const&  file_name() const noexcept {return file_name_;}
    std::string const&  line_str()  const noexcept {return line_str_;}

  private:

    std::uint_least32_t line_num_;
    std::uint_least32_t column_num_;
    std::uint_least32_t region_size_;
    std::string         file_name_;
    std::string         line_str_;
};

namespace detail
{

// internal error message generation.
inline std::string format_underline(const std::string& message,
        const std::vector<std::pair<source_location, std::string>>& loc_com,
        const std::vector<std::string>& helps = {},
        const bool colorize = TOML11_ERROR_MESSAGE_COLORIZED)
{
    std::size_t line_num_width = 0;
    for(const auto& lc : loc_com)
    {
        std::uint_least32_t line = lc.first.line();
        std::size_t        digit = 0;
        while(line != 0)
        {
            line  /= 10;
            digit +=  1;
        }
        line_num_width = (std::max)(line_num_width, digit);
    }
    // 1 is the minimum width
    line_num_width = std::max<std::size_t>(line_num_width, 1);

    std::ostringstream retval;

    if(color::should_color() || colorize)
    {
        retval << color::colorize; // turn on ANSI color
    }

    // XXX
    // Here, before `colorize` support, it does not output `[error]` prefix
    // automatically. So some user may output it manually and this change may
    // duplicate the prefix. To avoid it, check the first 7 characters and
    // if it is "[error]", it removes that part from the message shown.
    if(message.size() > 7 && message.substr(0, 7) == "[error]")
    {
        retval
#ifndef TOML11_NO_ERROR_PREFIX
               << color::bold << color::red << "[error]" << color::reset
#endif
               << color::bold << message.substr(7) << color::reset << '\n';
    }
    else
    {
        retval
#ifndef TOML11_NO_ERROR_PREFIX
               << color::bold << color::red << "[error] " << color::reset
#endif
               << color::bold << message << color::reset << '\n';
    }

    const auto format_one_location = [line_num_width]
        (std::ostringstream& oss,
         const source_location& loc, const std::string& comment) -> void
        {
            oss << ' ' << color::bold << color::blue
                << std::setw(static_cast<int>(line_num_width))
                << std::right << loc.line() << " | "  << color::reset
                << loc.line_str() << '\n';

            oss << make_string(line_num_width + 1, ' ')
                << color::bold << color::blue << " | " << color::reset
                << make_string(loc.column()-1 /*1-origin*/, ' ');

            if(loc.region() == 1)
            {
                // invalid
                // ^------
                oss << color::bold << color::red << "^---" << color::reset;
            }
            else
            {
                // invalid
                // ~~~~~~~
                const auto underline_len = (std::min)(
                    static_cast<std::size_t>(loc.region()), loc.line_str().size());
                oss << color::bold << color::red
                    << make_string(underline_len, '~') << color::reset;
            }
            oss << ' ';
            oss << comment;
            return;
        };

    assert(!loc_com.empty());

    // --> example.toml
    //   |
    retval << color::bold << color::blue << " --> " << color::reset
           << loc_com.front().first.file_name() << '\n';
    retval << make_string(line_num_width + 1, ' ')
           << color::bold << color::blue << " |\n"  << color::reset;
    // 1 | key value
    //   |    ^--- missing =
    format_one_location(retval, loc_com.front().first, loc_com.front().second);

    // process the rest of the locations
    for(std::size_t i=1; i<loc_com.size(); ++i)
    {
        const auto& prev = loc_com.at(i-1);
        const auto& curr = loc_com.at(i);

        retval << '\n';
        // if the filenames are the same, print "..."
        if(prev.first.file_name() == curr.first.file_name())
        {
            retval << color::bold << color::blue << " ...\n" << color::reset;
        }
        else // if filename differs, print " --> filename.toml" again
        {
            retval << color::bold << color::blue << " --> " << color::reset
                   << curr.first.file_name() << '\n';
            retval << make_string(line_num_width + 1, ' ')
                   << color::bold << color::blue << " |\n"  << color::reset;
        }

        format_one_location(retval, curr.first, curr.second);
    }

    if(!helps.empty())
    {
        retval << '\n';
        retval << make_string(line_num_width + 1, ' ');
        retval << color::bold << color::blue << " |" << color::reset;
        for(const auto& help : helps)
        {
            retval << color::bold << "\nHint: " << color::reset;
            retval << help;
        }
    }
    return retval.str();
}

} // detail
} // toml
#endif// TOML11_SOURCE_LOCATION_HPP
