#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/tests/libexpr.hh"

namespace nix {

using namespace testing;

// Testing eval of PrimOp's
class ErrorTraceTest : public LibExprTest
{};

TEST_F(ErrorTraceTest, TraceBuilder)
{
    ASSERT_THROW(state.error<EvalError>("puppy").debugThrow(), EvalError);

    ASSERT_THROW(state.error<EvalError>("puppy").withTrace(noPos, "doggy").debugThrow(), EvalError);

    ASSERT_THROW(
        try {
            try {
                state.error<EvalError>("puppy").withTrace(noPos, "doggy").debugThrow();
            } catch (Error & e) {
                e.addTrace(state.positions[noPos], "beans");
                throw;
            }
        } catch (BaseError & e) {
            ASSERT_EQ(PrintToString(e.info().msg), PrintToString(HintFmt("puppy")));
            auto trace = e.info().traces.rbegin();
            ASSERT_EQ(e.info().traces.size(), 2u);
            ASSERT_EQ(PrintToString(trace->hint), PrintToString(HintFmt("doggy")));
            trace++;
            ASSERT_EQ(PrintToString(trace->hint), PrintToString(HintFmt("beans")));
            throw;
        },
        EvalError);
}

TEST_F(ErrorTraceTest, NestedThrows)
{
    try {
        state.error<EvalError>("puppy").withTrace(noPos, "doggy").debugThrow();
    } catch (BaseError & e) {
        try {
            state.error<EvalError>("beans").debugThrow();
        } catch (Error & e2) {
            e.addTrace(state.positions[noPos], "beans2");
            // e2.addTrace(state.positions[noPos], "Something", "");
            ASSERT_TRUE(e.info().traces.size() == 2u);
            ASSERT_TRUE(e2.info().traces.size() == 0u);
            ASSERT_FALSE(&e.info() == &e2.info());
        }
    }
}

} /* namespace nix */
