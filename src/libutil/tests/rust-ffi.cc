#include "rust-ffi.hh"
#include <gtest/gtest.h>
#include <sstream>

namespace rust {

    /* ----------------------------------------------------------------------------
     * String
     * --------------------------------------------------------------------------*/

    TEST(RustString, constructString) {
        String s("afsjdkljfhreajkthawklfjrektghaekjfjhrgklhreak;rjwaeuifhasuifh");
        std::string_view v(s);
        ASSERT_EQ(v, "afsjdkljfhreajkthawklfjrektghaekjfjhrgklhreak;rjwaeuifhasuifh");
    }

    TEST(RustString, writeToStream) {
        String s("nlvkhrtiluwaejklfkjdthaewojfrldhnguirdag");
        std::string_view v(s);
        std::stringstream out;

        String s2(v);
        out << s2;

        ASSERT_EQ(out.str(), "nlvkhrtiluwaejklfkjdthaewojfrldhnguirdag");
    }

    /* ----------------------------------------------------------------------------
     * Result
     * --------------------------------------------------------------------------*/

    TEST(RustResult, resultCanBeUnwrapped) {
        Result<int> r;
        r.tag = Result<int>::Ok;
        r.data = 1;

        ASSERT_EQ(r.unwrap(), 1);
    }

    ///XXX: This is *NOT* what we want. We should be able
    //to set exc to an exception object here and then do
    //an ASSER_THROW(r.unwrap(), Error)
    TEST(RustResult, cannotUnwrapErrResult) {
        Result<int> r;
        r.tag = Result<int>::Err;
        r.exc = NULL;

        ASSERT_DEATH(r.unwrap(), "");

    }

}
