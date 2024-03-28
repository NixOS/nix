//     Copyright Toru Niina 2017.
// Distributed under the MIT License.
#ifndef TOML11_DATETIME_HPP
#define TOML11_DATETIME_HPP
#include <cstdint>
#include <cstdlib>
#include <ctime>

#include <array>
#include <chrono>
#include <iomanip>
#include <ostream>
#include <tuple>

namespace toml
{

// To avoid non-threadsafe std::localtime. In C11 (not C++11!), localtime_s is
// provided in the absolutely same purpose, but C++11 is actually not compatible
// with C11. We need to dispatch the function depending on the OS.
namespace detail
{
// TODO: find more sophisticated way to handle this
#if defined(_MSC_VER)
inline std::tm localtime_s(const std::time_t* src)
{
    std::tm dst;
    const auto result = ::localtime_s(&dst, src);
    if (result) { throw std::runtime_error("localtime_s failed."); }
    return dst;
}
inline std::tm gmtime_s(const std::time_t* src)
{
    std::tm dst;
    const auto result = ::gmtime_s(&dst, src);
    if (result) { throw std::runtime_error("gmtime_s failed."); }
    return dst;
}
#elif (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 1) || defined(_XOPEN_SOURCE) || defined(_BSD_SOURCE) || defined(_SVID_SOURCE) || defined(_POSIX_SOURCE)
inline std::tm localtime_s(const std::time_t* src)
{
    std::tm dst;
    const auto result = ::localtime_r(src, &dst);
    if (!result) { throw std::runtime_error("localtime_r failed."); }
    return dst;
}
inline std::tm gmtime_s(const std::time_t* src)
{
    std::tm dst;
    const auto result = ::gmtime_r(src, &dst);
    if (!result) { throw std::runtime_error("gmtime_r failed."); }
    return dst;
}
#else // fallback. not threadsafe
inline std::tm localtime_s(const std::time_t* src)
{
    const auto result = std::localtime(src);
    if (!result) { throw std::runtime_error("localtime failed."); }
    return *result;
}
inline std::tm gmtime_s(const std::time_t* src)
{
    const auto result = std::gmtime(src);
    if (!result) { throw std::runtime_error("gmtime failed."); }
    return *result;
}
#endif
} // detail

enum class month_t : std::uint8_t
{
    Jan =  0,
    Feb =  1,
    Mar =  2,
    Apr =  3,
    May =  4,
    Jun =  5,
    Jul =  6,
    Aug =  7,
    Sep =  8,
    Oct =  9,
    Nov = 10,
    Dec = 11
};

struct local_date
{
    std::int16_t year{};   // A.D. (like, 2018)
    std::uint8_t month{};  // [0, 11]
    std::uint8_t day{};    // [1, 31]

    local_date(int y, month_t m, int d)
        : year (static_cast<std::int16_t>(y)),
          month(static_cast<std::uint8_t>(m)),
          day  (static_cast<std::uint8_t>(d))
    {}

    explicit local_date(const std::tm& t)
        : year (static_cast<std::int16_t>(t.tm_year + 1900)),
          month(static_cast<std::uint8_t>(t.tm_mon)),
          day  (static_cast<std::uint8_t>(t.tm_mday))
    {}

    explicit local_date(const std::chrono::system_clock::time_point& tp)
    {
        const auto t    = std::chrono::system_clock::to_time_t(tp);
        const auto time = detail::localtime_s(&t);
        *this = local_date(time);
    }

    explicit local_date(const std::time_t t)
        : local_date(std::chrono::system_clock::from_time_t(t))
    {}

    operator std::chrono::system_clock::time_point() const
    {
        // std::mktime returns date as local time zone. no conversion needed
        std::tm t;
        t.tm_sec   = 0;
        t.tm_min   = 0;
        t.tm_hour  = 0;
        t.tm_mday  = static_cast<int>(this->day);
        t.tm_mon   = static_cast<int>(this->month);
        t.tm_year  = static_cast<int>(this->year) - 1900;
        t.tm_wday  = 0; // the value will be ignored
        t.tm_yday  = 0; // the value will be ignored
        t.tm_isdst = -1;
        return std::chrono::system_clock::from_time_t(std::mktime(&t));
    }

    operator std::time_t() const
    {
        return std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::time_point(*this));
    }

    local_date() = default;
    ~local_date() = default;
    local_date(local_date const&) = default;
    local_date(local_date&&)      = default;
    local_date& operator=(local_date const&) = default;
    local_date& operator=(local_date&&)      = default;
};

inline bool operator==(const local_date& lhs, const local_date& rhs)
{
    return std::make_tuple(lhs.year, lhs.month, lhs.day) ==
           std::make_tuple(rhs.year, rhs.month, rhs.day);
}
inline bool operator!=(const local_date& lhs, const local_date& rhs)
{
    return !(lhs == rhs);
}
inline bool operator< (const local_date& lhs, const local_date& rhs)
{
    return std::make_tuple(lhs.year, lhs.month, lhs.day) <
           std::make_tuple(rhs.year, rhs.month, rhs.day);
}
inline bool operator<=(const local_date& lhs, const local_date& rhs)
{
    return (lhs < rhs) || (lhs == rhs);
}
inline bool operator> (const local_date& lhs, const local_date& rhs)
{
    return !(lhs <= rhs);
}
inline bool operator>=(const local_date& lhs, const local_date& rhs)
{
    return !(lhs < rhs);
}

template<typename charT, typename traits>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const local_date& date)
{
    os << std::setfill('0') << std::setw(4) << static_cast<int>(date.year )     << '-';
    os << std::setfill('0') << std::setw(2) << static_cast<int>(date.month) + 1 << '-';
    os << std::setfill('0') << std::setw(2) << static_cast<int>(date.day  )    ;
    return os;
}

struct local_time
{
    std::uint8_t  hour{};        // [0, 23]
    std::uint8_t  minute{};      // [0, 59]
    std::uint8_t  second{};      // [0, 60]
    std::uint16_t millisecond{}; // [0, 999]
    std::uint16_t microsecond{}; // [0, 999]
    std::uint16_t nanosecond{};  // [0, 999]

    local_time(int h, int m, int s,
               int ms = 0, int us = 0, int ns = 0)
        : hour  (static_cast<std::uint8_t>(h)),
          minute(static_cast<std::uint8_t>(m)),
          second(static_cast<std::uint8_t>(s)),
          millisecond(static_cast<std::uint16_t>(ms)),
          microsecond(static_cast<std::uint16_t>(us)),
          nanosecond (static_cast<std::uint16_t>(ns))
    {}

    explicit local_time(const std::tm& t)
        : hour  (static_cast<std::uint8_t>(t.tm_hour)),
          minute(static_cast<std::uint8_t>(t.tm_min)),
          second(static_cast<std::uint8_t>(t.tm_sec)),
          millisecond(0), microsecond(0), nanosecond(0)
    {}

    template<typename Rep, typename Period>
    explicit local_time(const std::chrono::duration<Rep, Period>& t)
    {
        const auto h = std::chrono::duration_cast<std::chrono::hours>(t);
        this->hour = static_cast<std::uint8_t>(h.count());
        const auto t2 = t - h;
        const auto m = std::chrono::duration_cast<std::chrono::minutes>(t2);
        this->minute = static_cast<std::uint8_t>(m.count());
        const auto t3 = t2 - m;
        const auto s = std::chrono::duration_cast<std::chrono::seconds>(t3);
        this->second = static_cast<std::uint8_t>(s.count());
        const auto t4 = t3 - s;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t4);
        this->millisecond = static_cast<std::uint16_t>(ms.count());
        const auto t5 = t4 - ms;
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t5);
        this->microsecond = static_cast<std::uint16_t>(us.count());
        const auto t6 = t5 - us;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t6);
        this->nanosecond = static_cast<std::uint16_t>(ns.count());
    }

    operator std::chrono::nanoseconds() const
    {
        return std::chrono::nanoseconds (this->nanosecond)  +
               std::chrono::microseconds(this->microsecond) +
               std::chrono::milliseconds(this->millisecond) +
               std::chrono::seconds(this->second) +
               std::chrono::minutes(this->minute) +
               std::chrono::hours(this->hour);
    }

    local_time() = default;
    ~local_time() = default;
    local_time(local_time const&) = default;
    local_time(local_time&&)      = default;
    local_time& operator=(local_time const&) = default;
    local_time& operator=(local_time&&)      = default;
};

inline bool operator==(const local_time& lhs, const local_time& rhs)
{
    return std::make_tuple(lhs.hour, lhs.minute, lhs.second, lhs.millisecond, lhs.microsecond, lhs.nanosecond) ==
           std::make_tuple(rhs.hour, rhs.minute, rhs.second, rhs.millisecond, rhs.microsecond, rhs.nanosecond);
}
inline bool operator!=(const local_time& lhs, const local_time& rhs)
{
    return !(lhs == rhs);
}
inline bool operator< (const local_time& lhs, const local_time& rhs)
{
    return std::make_tuple(lhs.hour, lhs.minute, lhs.second, lhs.millisecond, lhs.microsecond, lhs.nanosecond) <
           std::make_tuple(rhs.hour, rhs.minute, rhs.second, rhs.millisecond, rhs.microsecond, rhs.nanosecond);
}
inline bool operator<=(const local_time& lhs, const local_time& rhs)
{
    return (lhs < rhs) || (lhs == rhs);
}
inline bool operator> (const local_time& lhs, const local_time& rhs)
{
    return !(lhs <= rhs);
}
inline bool operator>=(const local_time& lhs, const local_time& rhs)
{
    return !(lhs < rhs);
}

template<typename charT, typename traits>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const local_time& time)
{
    os << std::setfill('0') << std::setw(2) << static_cast<int>(time.hour  ) << ':';
    os << std::setfill('0') << std::setw(2) << static_cast<int>(time.minute) << ':';
    os << std::setfill('0') << std::setw(2) << static_cast<int>(time.second);
    if(time.millisecond != 0 || time.microsecond != 0 || time.nanosecond != 0)
    {
        os << '.';
        os << std::setfill('0') << std::setw(3) << static_cast<int>(time.millisecond);
        if(time.microsecond != 0 || time.nanosecond != 0)
        {
            os << std::setfill('0') << std::setw(3) << static_cast<int>(time.microsecond);
            if(time.nanosecond != 0)
            {
                os << std::setfill('0') << std::setw(3) << static_cast<int>(time.nanosecond);
            }
        }
    }
    return os;
}

struct time_offset
{
    std::int8_t hour{};   // [-12, 12]
    std::int8_t minute{}; // [-59, 59]

    time_offset(int h, int m)
        : hour  (static_cast<std::int8_t>(h)),
          minute(static_cast<std::int8_t>(m))
    {}

    operator std::chrono::minutes() const
    {
        return std::chrono::minutes(this->minute) +
               std::chrono::hours(this->hour);
    }

    time_offset() = default;
    ~time_offset() = default;
    time_offset(time_offset const&) = default;
    time_offset(time_offset&&)      = default;
    time_offset& operator=(time_offset const&) = default;
    time_offset& operator=(time_offset&&)      = default;
};

inline bool operator==(const time_offset& lhs, const time_offset& rhs)
{
    return std::make_tuple(lhs.hour, lhs.minute) ==
           std::make_tuple(rhs.hour, rhs.minute);
}
inline bool operator!=(const time_offset& lhs, const time_offset& rhs)
{
    return !(lhs == rhs);
}
inline bool operator< (const time_offset& lhs, const time_offset& rhs)
{
    return std::make_tuple(lhs.hour, lhs.minute) <
           std::make_tuple(rhs.hour, rhs.minute);
}
inline bool operator<=(const time_offset& lhs, const time_offset& rhs)
{
    return (lhs < rhs) || (lhs == rhs);
}
inline bool operator> (const time_offset& lhs, const time_offset& rhs)
{
    return !(lhs <= rhs);
}
inline bool operator>=(const time_offset& lhs, const time_offset& rhs)
{
    return !(lhs < rhs);
}

template<typename charT, typename traits>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const time_offset& offset)
{
    if(offset.hour == 0 && offset.minute == 0)
    {
        os << 'Z';
        return os;
    }
    int minute = static_cast<int>(offset.hour) * 60 + offset.minute;
    if(minute < 0){os << '-'; minute = std::abs(minute);} else {os << '+';}
    os << std::setfill('0') << std::setw(2) << minute / 60 << ':';
    os << std::setfill('0') << std::setw(2) << minute % 60;
    return os;
}

struct local_datetime
{
    local_date date{};
    local_time time{};

    local_datetime(local_date d, local_time t): date(d), time(t) {}

    explicit local_datetime(const std::tm& t): date(t), time(t){}

    explicit local_datetime(const std::chrono::system_clock::time_point& tp)
    {
        const auto t = std::chrono::system_clock::to_time_t(tp);
        std::tm ltime = detail::localtime_s(&t);

        this->date = local_date(ltime);
        this->time = local_time(ltime);

        // std::tm lacks subsecond information, so diff between tp and tm
        // can be used to get millisecond & microsecond information.
        const auto t_diff = tp -
            std::chrono::system_clock::from_time_t(std::mktime(&ltime));
        this->time.millisecond = static_cast<std::uint16_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(t_diff).count());
        this->time.microsecond = static_cast<std::uint16_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(t_diff).count());
        this->time.nanosecond = static_cast<std::uint16_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds >(t_diff).count());
    }

    explicit local_datetime(const std::time_t t)
        : local_datetime(std::chrono::system_clock::from_time_t(t))
    {}

    operator std::chrono::system_clock::time_point() const
    {
        using internal_duration =
            typename std::chrono::system_clock::time_point::duration;

        // Normally DST begins at A.M. 3 or 4. If we re-use conversion operator
        // of local_date and local_time independently, the conversion fails if
        // it is the day when DST begins or ends. Since local_date considers the
        // time is 00:00 A.M. and local_time does not consider DST because it
        // does not have any date information. We need to consider both date and
        // time information at the same time to convert it correctly.

        std::tm t;
        t.tm_sec   = static_cast<int>(this->time.second);
        t.tm_min   = static_cast<int>(this->time.minute);
        t.tm_hour  = static_cast<int>(this->time.hour);
        t.tm_mday  = static_cast<int>(this->date.day);
        t.tm_mon   = static_cast<int>(this->date.month);
        t.tm_year  = static_cast<int>(this->date.year) - 1900;
        t.tm_wday  = 0; // the value will be ignored
        t.tm_yday  = 0; // the value will be ignored
        t.tm_isdst = -1;

        // std::mktime returns date as local time zone. no conversion needed
        auto dt = std::chrono::system_clock::from_time_t(std::mktime(&t));
        dt += std::chrono::duration_cast<internal_duration>(
                std::chrono::milliseconds(this->time.millisecond) +
                std::chrono::microseconds(this->time.microsecond) +
                std::chrono::nanoseconds (this->time.nanosecond));
        return dt;
    }

    operator std::time_t() const
    {
        return std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::time_point(*this));
    }

    local_datetime() = default;
    ~local_datetime() = default;
    local_datetime(local_datetime const&) = default;
    local_datetime(local_datetime&&)      = default;
    local_datetime& operator=(local_datetime const&) = default;
    local_datetime& operator=(local_datetime&&)      = default;
};

inline bool operator==(const local_datetime& lhs, const local_datetime& rhs)
{
    return std::make_tuple(lhs.date, lhs.time) ==
           std::make_tuple(rhs.date, rhs.time);
}
inline bool operator!=(const local_datetime& lhs, const local_datetime& rhs)
{
    return !(lhs == rhs);
}
inline bool operator< (const local_datetime& lhs, const local_datetime& rhs)
{
    return std::make_tuple(lhs.date, lhs.time) <
           std::make_tuple(rhs.date, rhs.time);
}
inline bool operator<=(const local_datetime& lhs, const local_datetime& rhs)
{
    return (lhs < rhs) || (lhs == rhs);
}
inline bool operator> (const local_datetime& lhs, const local_datetime& rhs)
{
    return !(lhs <= rhs);
}
inline bool operator>=(const local_datetime& lhs, const local_datetime& rhs)
{
    return !(lhs < rhs);
}

template<typename charT, typename traits>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const local_datetime& dt)
{
    os << dt.date << 'T' << dt.time;
    return os;
}

struct offset_datetime
{
    local_date  date{};
    local_time  time{};
    time_offset offset{};

    offset_datetime(local_date d, local_time t, time_offset o)
        : date(d), time(t), offset(o)
    {}
    offset_datetime(const local_datetime& dt, time_offset o)
        : date(dt.date), time(dt.time), offset(o)
    {}
    explicit offset_datetime(const local_datetime& ld)
        : date(ld.date), time(ld.time), offset(get_local_offset(nullptr))
          // use the current local timezone offset
    {}
    explicit offset_datetime(const std::chrono::system_clock::time_point& tp)
        : offset(0, 0) // use gmtime
    {
        const auto timet = std::chrono::system_clock::to_time_t(tp);
        const auto tm    = detail::gmtime_s(&timet);
        this->date = local_date(tm);
        this->time = local_time(tm);
    }
    explicit offset_datetime(const std::time_t& t)
        : offset(0, 0) // use gmtime
    {
        const auto tm    = detail::gmtime_s(&t);
        this->date = local_date(tm);
        this->time = local_time(tm);
    }
    explicit offset_datetime(const std::tm& t)
        : offset(0, 0) // assume gmtime
    {
        this->date = local_date(t);
        this->time = local_time(t);
    }

    operator std::chrono::system_clock::time_point() const
    {
        // get date-time
        using internal_duration =
            typename std::chrono::system_clock::time_point::duration;

        // first, convert it to local date-time information in the same way as
        // local_datetime does. later we will use time_t to adjust time offset.
        std::tm t;
        t.tm_sec   = static_cast<int>(this->time.second);
        t.tm_min   = static_cast<int>(this->time.minute);
        t.tm_hour  = static_cast<int>(this->time.hour);
        t.tm_mday  = static_cast<int>(this->date.day);
        t.tm_mon   = static_cast<int>(this->date.month);
        t.tm_year  = static_cast<int>(this->date.year) - 1900;
        t.tm_wday  = 0; // the value will be ignored
        t.tm_yday  = 0; // the value will be ignored
        t.tm_isdst = -1;
        const std::time_t tp_loc = std::mktime(std::addressof(t));

        auto tp = std::chrono::system_clock::from_time_t(tp_loc);
        tp += std::chrono::duration_cast<internal_duration>(
                std::chrono::milliseconds(this->time.millisecond) +
                std::chrono::microseconds(this->time.microsecond) +
                std::chrono::nanoseconds (this->time.nanosecond));

        // Since mktime uses local time zone, it should be corrected.
        // `12:00:00+09:00` means `03:00:00Z`. So mktime returns `03:00:00Z` if
        // we are in `+09:00` timezone. To represent `12:00:00Z` there, we need
        // to add `+09:00` to `03:00:00Z`.
        //    Here, it uses the time_t converted from date-time info to handle
        // daylight saving time.
        const auto ofs = get_local_offset(std::addressof(tp_loc));
        tp += std::chrono::hours  (ofs.hour);
        tp += std::chrono::minutes(ofs.minute);

        // We got `12:00:00Z` by correcting local timezone applied by mktime.
        // Then we will apply the offset. Let's say `12:00:00-08:00` is given.
        // And now, we have `12:00:00Z`. `12:00:00-08:00` means `20:00:00Z`.
        // So we need to subtract the offset.
        tp -= std::chrono::minutes(this->offset);
        return tp;
    }

    operator std::time_t() const
    {
        return std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::time_point(*this));
    }

    offset_datetime() = default;
    ~offset_datetime() = default;
    offset_datetime(offset_datetime const&) = default;
    offset_datetime(offset_datetime&&)      = default;
    offset_datetime& operator=(offset_datetime const&) = default;
    offset_datetime& operator=(offset_datetime&&)      = default;

  private:

    static time_offset get_local_offset(const std::time_t* tp)
    {
        // get local timezone with the same date-time information as mktime
        const auto t = detail::localtime_s(tp);

        std::array<char, 6> buf;
        const auto result = std::strftime(buf.data(), 6, "%z", &t); // +hhmm\0
        if(result != 5)
        {
            throw std::runtime_error("toml::offset_datetime: cannot obtain "
                                     "timezone information of current env");
        }
        const int ofs = std::atoi(buf.data());
        const int ofs_h = ofs / 100;
        const int ofs_m = ofs - (ofs_h * 100);
        return time_offset(ofs_h, ofs_m);
    }
};

inline bool operator==(const offset_datetime& lhs, const offset_datetime& rhs)
{
    return std::make_tuple(lhs.date, lhs.time, lhs.offset) ==
           std::make_tuple(rhs.date, rhs.time, rhs.offset);
}
inline bool operator!=(const offset_datetime& lhs, const offset_datetime& rhs)
{
    return !(lhs == rhs);
}
inline bool operator< (const offset_datetime& lhs, const offset_datetime& rhs)
{
    return std::make_tuple(lhs.date, lhs.time, lhs.offset) <
           std::make_tuple(rhs.date, rhs.time, rhs.offset);
}
inline bool operator<=(const offset_datetime& lhs, const offset_datetime& rhs)
{
    return (lhs < rhs) || (lhs == rhs);
}
inline bool operator> (const offset_datetime& lhs, const offset_datetime& rhs)
{
    return !(lhs <= rhs);
}
inline bool operator>=(const offset_datetime& lhs, const offset_datetime& rhs)
{
    return !(lhs < rhs);
}

template<typename charT, typename traits>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const offset_datetime& dt)
{
    os << dt.date << 'T' << dt.time << dt.offset;
    return os;
}

}//toml
#endif// TOML11_DATETIME
