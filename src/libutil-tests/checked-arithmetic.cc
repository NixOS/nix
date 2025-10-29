#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <rapidcheck/Assertions.h>
#include <rapidcheck/gtest.h>
#include <rapidcheck/gen/Arbitrary.hpp>

#include "nix/util/checked-arithmetic.hh"

#include "nix/util/tests/gtest-with-params.hh"

namespace rc {
using namespace nix;

template<std::integral T>
struct Arbitrary<nix::checked::Checked<T>>
{
    static Gen<nix::checked::Checked<T>> arbitrary()
    {
        return gen::arbitrary<T>();
    }
};

} // namespace rc

namespace nix::checked {

// Pointer to member function! Mildly gross.
template<std::integral T>
using Oper = Checked<T>::Result (Checked<T>::*)(T const other) const;

template<std::integral T>
using ReferenceOper = T (*)(T a, T b);

/**
 * Checks that performing an operation that overflows into an inaccurate result
 * has the desired behaviour.
 *
 * TBig is a type large enough to represent all results of TSmall operations.
 */
template<std::integral TSmall, std::integral TBig>
void checkType(TSmall a_, TSmall b, Oper<TSmall> oper, ReferenceOper<TBig> reference)
{
    // Sufficient to fit all values
    TBig referenceResult = reference(a_, b);
    constexpr const TSmall minV = std::numeric_limits<TSmall>::min();
    constexpr const TSmall maxV = std::numeric_limits<TSmall>::max();

    Checked<TSmall> a{a_};
    auto result = (a.*(oper))(b);

    // Just truncate it to get the in-range result
    RC_ASSERT(result.valueWrapping() == static_cast<TSmall>(referenceResult));

    if (referenceResult > maxV || referenceResult < minV) {
        RC_ASSERT(result.overflowed());
        RC_ASSERT(!result.valueChecked().has_value());
    } else {
        RC_ASSERT(!result.overflowed());
        RC_ASSERT(result.valueChecked().has_value());
        RC_ASSERT(*result.valueChecked() == referenceResult);
    }
}

/**
 * Checks that performing an operation that overflows into an inaccurate result
 * has the desired behaviour.
 *
 * TBig is a type large enough to represent all results of TSmall operations.
 */
template<std::integral TSmall, std::integral TBig>
void checkDivision(TSmall a_, TSmall b)
{
    // Sufficient to fit all values
    constexpr const TSmall minV = std::numeric_limits<TSmall>::min();

    Checked<TSmall> a{a_};
    auto result = a / b;

    if (std::is_signed<TSmall>() && a_ == minV && b == -1) {
        // This is the only possible overflow condition
        RC_ASSERT(result.valueWrapping() == minV);
        RC_ASSERT(result.overflowed());
    } else if (b == 0) {
        RC_ASSERT(result.divideByZero());
        RC_ASSERT_THROWS_AS(result.valueWrapping(), nix::checked::DivideByZero);
        RC_ASSERT(result.valueChecked() == std::nullopt);
    } else {
        TBig referenceResult = a_ / b;
        auto result_ = result.valueChecked();
        RC_ASSERT(result_.has_value());
        RC_ASSERT(*result_ == referenceResult);
        RC_ASSERT(result.valueWrapping() == referenceResult);
    }
}

/** Creates parameters that perform a more adequate number of checks to validate
 * extremely cheap tests such as arithmetic tests */
static rc::detail::TestParams makeParams()
{
    auto const & conf = rc::detail::configuration();
    auto newParams = conf.testParams;
    newParams.maxSuccess = 10000;
    return newParams;
}

RC_GTEST_PROP_WITH_PARAMS(Checked, add_unsigned, makeParams, (uint16_t a, uint16_t b))
{
    checkType<uint16_t, int32_t>(a, b, &Checked<uint16_t>::operator+, [](int32_t a, int32_t b) { return a + b; });
}

RC_GTEST_PROP_WITH_PARAMS(Checked, add_signed, makeParams, (int16_t a, int16_t b))
{
    checkType<int16_t, int32_t>(a, b, &Checked<int16_t>::operator+, [](int32_t a, int32_t b) { return a + b; });
}

RC_GTEST_PROP_WITH_PARAMS(Checked, sub_unsigned, makeParams, (uint16_t a, uint16_t b))
{
    checkType<uint16_t, int32_t>(a, b, &Checked<uint16_t>::operator-, [](int32_t a, int32_t b) { return a - b; });
}

RC_GTEST_PROP_WITH_PARAMS(Checked, sub_signed, makeParams, (int16_t a, int16_t b))
{
    checkType<int16_t, int32_t>(a, b, &Checked<int16_t>::operator-, [](int32_t a, int32_t b) { return a - b; });
}

RC_GTEST_PROP_WITH_PARAMS(Checked, mul_unsigned, makeParams, (uint16_t a, uint16_t b))
{
    checkType<uint16_t, int64_t>(a, b, &Checked<uint16_t>::operator*, [](int64_t a, int64_t b) { return a * b; });
}

RC_GTEST_PROP_WITH_PARAMS(Checked, mul_signed, makeParams, (int16_t a, int16_t b))
{
    checkType<int16_t, int64_t>(a, b, &Checked<int16_t>::operator*, [](int64_t a, int64_t b) { return a * b; });
}

RC_GTEST_PROP_WITH_PARAMS(Checked, div_unsigned, makeParams, (uint16_t a, uint16_t b))
{
    checkDivision<uint16_t, int64_t>(a, b);
}

RC_GTEST_PROP_WITH_PARAMS(Checked, div_signed, makeParams, (int16_t a, int16_t b))
{
    checkDivision<int16_t, int64_t>(a, b);
}

// Make absolutely sure that we check the special cases if the proptest
// generator does not come up with them. This one is especially important
// because it has very specific pairs required for the edge cases unlike the
// others.
TEST(Checked, div_signed_special_cases)
{
    checkDivision<int16_t, int64_t>(std::numeric_limits<int16_t>::min(), -1);
    checkDivision<int16_t, int64_t>(std::numeric_limits<int16_t>::min(), 0);
    checkDivision<int16_t, int64_t>(0, 0);
}

} // namespace nix::checked
