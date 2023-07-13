#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "tests/libexpr.hh"
#include "tests/value.hh"

#include <limits>
#include <sstream>

#include "value-to-toml.hh"
#include "value.hh"

namespace nix {
    // Testing conversion to TOML

    class TOMLValueTest : public LibExprTest {
        protected:
            std::string getTOMLValue(Value& value) {
                std::stringstream ss;
                NixStringContext ps;
                printValueAsTOML(state, true, value, noPos, ss, ps);
                return ss.str();
            }
    };

    TEST_F(TOMLValueTest, BoolFalse) {
        Value v;
        v.mkBool(false);
        ASSERT_EQ(getTOMLValue(v),"false");
    }

    TEST_F(TOMLValueTest, BoolTrue) {
        Value v;
        v.mkBool(true);
        ASSERT_EQ(getTOMLValue(v), "true");
    }

    TEST_F(TOMLValueTest, IntPositive) {
        Value v;
        v.mkInt(100);
        ASSERT_EQ(getTOMLValue(v), "100");
    }

    TEST_F(TOMLValueTest, IntNegative) {
        Value v;
        v.mkInt(-100);
        ASSERT_EQ(getTOMLValue(v), "-100");
    }

    TEST_F(TOMLValueTest, FloatPositive) {
        Value v;
        v.mkFloat(6.6743);
        ASSERT_EQ(getTOMLValue(v), "6.6743");
    }

    TEST_F(TOMLValueTest, FloatNegative) {
        Value v;
        v.mkFloat(-6.6743);
        ASSERT_EQ(getTOMLValue(v), "-6.6743");
    }

    TEST_F(TOMLValueTest, FloatPositiveInfinity) {
        Value v;
        v.mkFloat(std::numeric_limits<NixFloat>::infinity());
        ASSERT_EQ(getTOMLValue(v), "inf");
    }

    TEST_F(TOMLValueTest, FloatNegativeInfinity) {
        Value v;
        v.mkFloat(-std::numeric_limits<NixFloat>::infinity());
        ASSERT_EQ(getTOMLValue(v), "-inf");
    }

    TEST_F(TOMLValueTest, FloatNaN) {
        Value v;
        v.mkFloat(std::numeric_limits<NixFloat>::quiet_NaN());
        ASSERT_EQ(getTOMLValue(v), "nan");
    }

    TEST_F(TOMLValueTest, String) {
        Value v;
        v.mkString("foobar");
        ASSERT_EQ(getTOMLValue(v), "\"foobar\"");
    }

    // toml11 prints strings with double quote or linefeed characters as a
    // multi-line string, this is standard compliant but makes the conversion
    // test somewhat fragile. see https://github.com/ToruNiina/toml11/blob/master/toml/serializer.hpp
    TEST_F(TOMLValueTest, StringQuotes) {
        Value v;
        v.mkString("\"foobar\"");
        ASSERT_EQ(getTOMLValue(v), "\"\"\"\n\"foobar\"\\\n\"\"\"");
    }

    RC_GTEST_FIXTURE_PROP(
        TOMLValueTest,
        prop_round_trip,
        ())
    {
        auto v1 = *rc::genTOMLSerializableNixValue(state);
        RC_PRE(v1.type() == nAttrs);

        std::stringstream v1_ss;
        v1.print(state.symbols, v1_ss);

        std::stringstream ss;
        NixStringContext context;
        printValueAsTOML(state, true, const_cast<Value &>(v1), noPos, ss, context);
        Value temp, v2;
        temp.mkString(ss.str());
        state.callFunction(state.getBuiltin("fromTOML"), temp, v2, noPos);

        std::stringstream v2_ss;
        v2.print(state.symbols, v2_ss);

        RC_ASSERT(v1_ss.str() == v2_ss.str());
    }
} /* namespace nix */

