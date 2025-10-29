#pragma once
/**
 * @file
 *
 * Checked arithmetic with classes that make it hard to accidentally make something an unchecked operation.
 */

#include <compare>
#include <concepts> // IWYU pragma: keep
#include <exception>
#include <ostream>
#include <limits>
#include <optional>
#include <type_traits>

namespace nix::checked {

class DivideByZero : std::exception
{};

/**
 * Numeric value enforcing checked arithmetic. Performing mathematical operations on such values will return a Result
 * type which needs to be checked.
 */
template<std::integral T>
struct Checked
{
    using Inner = T;

    // TODO: this must be a "trivial default constructor", which means it
    // cannot set the value to NOT DO UB on uninit.
    T value;

    Checked() = default;

    explicit Checked(T const value)
        : value{value}
    {
    }

    Checked(Checked<T> const & other) = default;
    Checked(Checked<T> && other) = default;
    Checked<T> & operator=(Checked<T> const & other) = default;

    std::strong_ordering operator<=>(Checked<T> const & other) const = default;

    std::strong_ordering operator<=>(T const & other) const
    {
        return value <=> other;
    }

    explicit operator T() const
    {
        return value;
    }

    enum class OverflowKind {
        NoOverflow,
        Overflow,
        DivByZero,
    };

    class Result
    {
        T value;
        OverflowKind overflowed_;

    public:
        Result(T value, bool overflowed)
            : value{value}
            , overflowed_{overflowed ? OverflowKind::Overflow : OverflowKind::NoOverflow}
        {
        }

        Result(T value, OverflowKind overflowed)
            : value{value}
            , overflowed_{overflowed}
        {
        }

        bool operator==(Result other) const
        {
            return value == other.value && overflowed_ == other.overflowed_;
        }

        std::optional<T> valueChecked() const
        {
            if (overflowed_ != OverflowKind::NoOverflow) {
                return std::nullopt;
            } else {
                return value;
            }
        }

        /**
         * Returns the result as if the arithmetic were performed as wrapping arithmetic.
         *
         * \throws DivideByZero if the operation was a divide by zero.
         */
        T valueWrapping() const
        {
            if (overflowed_ == OverflowKind::DivByZero) {
                throw DivideByZero{};
            }
            return value;
        }

        bool overflowed() const
        {
            return overflowed_ == OverflowKind::Overflow;
        }

        bool divideByZero() const
        {
            return overflowed_ == OverflowKind::DivByZero;
        }
    };

    Result operator+(Checked<T> const other) const
    {
        return (*this) + other.value;
    }

    Result operator+(T const other) const
    {
        T result;
        bool overflowed = __builtin_add_overflow(value, other, &result);
        return Result{result, overflowed};
    }

    Result operator-(Checked<T> const other) const
    {
        return (*this) - other.value;
    }

    Result operator-(T const other) const
    {
        T result;
        bool overflowed = __builtin_sub_overflow(value, other, &result);
        return Result{result, overflowed};
    }

    Result operator*(Checked<T> const other) const
    {
        return (*this) * other.value;
    }

    Result operator*(T const other) const
    {
        T result;
        bool overflowed = __builtin_mul_overflow(value, other, &result);
        return Result{result, overflowed};
    }

    Result operator/(Checked<T> const other) const
    {
        return (*this) / other.value;
    }

    /**
     * Performs a checked division.
     *
     * If the right hand side is zero, the result is marked as a DivByZero and
     * valueWrapping will throw.
     */
    Result operator/(T const other) const
    {
        constexpr T const minV = std::numeric_limits<T>::min();

        // It's only possible to overflow with signed division since doing so
        // requires crossing the two's complement limits by MIN / -1 (since
        // two's complement has one more in range in the negative direction
        // than in the positive one).
        if (std::is_signed<T>() && (value == minV && other == -1)) {
            return Result{minV, true};
        } else if (other == 0) {
            return Result{0, OverflowKind::DivByZero};
        } else {
            T result = value / other;
            return Result{result, false};
        }
    }
};

template<std::integral T>
std::ostream & operator<<(std::ostream & ios, Checked<T> v)
{
    ios << v.value;
    return ios;
}

} // namespace nix::checked
