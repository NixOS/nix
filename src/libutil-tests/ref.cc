#include <gtest/gtest.h>
#include <type_traits>

#include "nix/util/demangle.hh"
#include "nix/util/ref.hh"

namespace nix {

// Test hierarchy for ref covariance tests
struct Base
{
    virtual ~Base() = default;
};

struct Derived : Base
{};

TEST(ref, upcast_is_implicit)
{
    // ref<Derived> should be implicitly convertible to ref<Base>
    static_assert(std::is_convertible_v<ref<Derived>, ref<Base>>);

    // Runtime test
    auto derived = make_ref<Derived>();
    ref<Base> base = derived; // implicit upcast
    EXPECT_NE(&*base, nullptr);
}

TEST(ref, downcast_is_rejected)
{
    // ref<Base> should NOT be implicitly convertible to ref<Derived>
    static_assert(!std::is_convertible_v<ref<Base>, ref<Derived>>);

    // Uncomment to see error message:
    // auto base = make_ref<Base>();
    // ref<Derived> d = base;
}

TEST(ref, same_type_conversion)
{
    // ref<T> should be convertible to ref<T>
    static_assert(std::is_convertible_v<ref<Base>, ref<Base>>);
    static_assert(std::is_convertible_v<ref<Derived>, ref<Derived>>);
}

TEST(ref, explicit_downcast_with_cast)
{
    // .cast() should work for valid downcasts at runtime
    auto derived = make_ref<Derived>();
    ref<Base> base = derived;

    // Downcast back to Derived using .cast()
    ref<Derived> backToDerived = base.cast<Derived>();
    EXPECT_NE(&*backToDerived, nullptr);
}

TEST(ref, invalid_cast_throws)
{
    // .cast() throws bad_ref_cast (a std::bad_cast subclass) with type info on invalid downcast
    // (unlike .dynamic_pointer_cast() which returns nullptr)
    auto base = make_ref<Base>();
    try {
        base.cast<Derived>();
        FAIL() << "Expected bad_ref_cast";
    } catch (const bad_ref_cast & e) {
        std::string expected = "ref<" + demangle(typeid(Base).name()) + "> cannot be cast to ref<"
                               + demangle(typeid(Derived).name()) + ">";
        EXPECT_EQ(e.what(), expected);
    }
}

TEST(ref, explicit_downcast_with_dynamic_pointer_cast)
{
    // .dynamic_pointer_cast() returns nullptr for invalid casts
    auto base = make_ref<Base>();

    // Invalid downcast returns nullptr
    auto invalidCast = base.dynamic_pointer_cast<Derived>();
    EXPECT_EQ(invalidCast, nullptr);

    // Valid downcast returns non-null
    auto derived = make_ref<Derived>();
    ref<Base> baseFromDerived = derived;
    auto validCast = baseFromDerived.dynamic_pointer_cast<Derived>();
    EXPECT_NE(validCast, nullptr);
}

} // namespace nix
