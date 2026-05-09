#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/tests/libexpr.hh"
#include "nix/util/error.hh"
#include "nix/util/position.hh"
#include "nix/util/terminal.hh"

#include <sstream>

namespace nix {

using namespace ::testing;

/**
 * Test fixture for evaluating Nix expressions and structurally inspecting
 * the resulting error traces.
 *
 * The traces on a BaseError represent the evaluation spine — a single path
 * in the dependency graph between expressions. This fixture provides helpers
 * to assert the presence, absence, and ordering of trace frames without
 * relying on full string matching of rendered output.
 */
class EvalTraceTest : public LibExprTest
{
protected:
    /**
     * A simplified view of one trace frame, for easy assertion.
     * Strips ANSI escapes so tests are rendering-independent.
     */
    struct Frame
    {
        std::string hint;
        bool hasPos;
        TracePrint print;
    };

    /**
     * Evaluate a Nix expression that is expected to fail,
     * and return the structured trace frames from the caught error.
     */
    std::vector<Frame> evalTraces(const std::string & expr)
    {
        try {
            eval(expr);
            ADD_FAILURE() << "Expected evaluation of `" << expr << "` to throw";
            return {};
        } catch (BaseError & e) {
            return extractFrames(e);
        }
    }

    /**
     * Evaluate a Nix expression that is expected to fail,
     * and return the ErrorInfo for direct inspection.
     */
    ErrorInfo evalErrorInfo(const std::string & expr)
    {
        try {
            eval(expr);
            ADD_FAILURE() << "Expected evaluation of `" << expr << "` to throw";
            return ErrorInfo{.level = lvlError, .msg = HintFmt("no error")};
        } catch (BaseError & e) {
            return e.info();
        }
    }

    /**
     * Extract Frame structs from a caught error.
     */
    static std::vector<Frame> extractFrames(const BaseError & e)
    {
        std::vector<Frame> frames;
        for (const auto & t : e.info().traces) {
            frames.push_back(Frame{
                .hint = filterANSIEscapes(t.hint.str(), true),
                .hasPos = t.pos && *t.pos,
                .print = t.print,
            });
        }
        return frames;
    }

    /**
     * Extract just the hint strings from frames, for concise assertions.
     */
    static std::vector<std::string> hints(const std::vector<Frame> & frames)
    {
        std::vector<std::string> out;
        for (auto & f : frames)
            out.push_back(f.hint);
        return out;
    }

    /**
     * Check if any frame's hint contains the given substring.
     */
    static bool hasTraceContaining(const std::vector<Frame> & frames, const std::string & substr)
    {
        for (auto & f : frames) {
            if (f.hint.find(substr) != std::string::npos)
                return true;
        }
        return false;
    }

    /**
     * Return the index of the first frame whose hint contains `substr`,
     * or -1 if not found.
     */
    static int findTrace(const std::vector<Frame> & frames, const std::string & substr)
    {
        for (size_t i = 0; i < frames.size(); i++) {
            if (frames[i].hint.find(substr) != std::string::npos)
                return static_cast<int>(i);
        }
        return -1;
    }

    /**
     * Render an ErrorInfo to a plain string (ANSI stripped),
     * for verifying the final output layout.
     */
    static std::string renderError(const ErrorInfo & einfo, bool showTrace)
    {
        std::ostringstream oss;
        showErrorInfo(oss, einfo, showTrace);
        return filterANSIEscapes(oss.str(), true);
    }

    /**
     * Find the position of a substring in rendered output, or std::string::npos.
     */
    static size_t findInOutput(const std::string & output, const std::string & substr)
    {
        return output.find(substr);
    }
};

// --- Basic trace presence/absence ---

TEST_F(EvalTraceTest, throwProducesBuiltinTrace)
{
    auto frames = evalTraces("throw \"oops\"");
    // `throw` is a builtin, so it adds a "while calling the 'throw' builtin" trace
    EXPECT_TRUE(hasTraceContaining(frames, "throw"));
}

TEST_F(EvalTraceTest, addErrorContextAppearsInTrace)
{
    auto frames = evalTraces(R"(
        builtins.addErrorContext "my context" (throw "inner")
    )");
    EXPECT_TRUE(hasTraceContaining(frames, "my context"));

    // addErrorContext traces should have TracePrint::Always
    for (auto & f : frames) {
        if (f.hint.find("my context") != std::string::npos) {
            EXPECT_EQ(f.print, TracePrint::Always);
        }
    }
}

TEST_F(EvalTraceTest, nestedAddErrorContextOrder)
{
    auto frames = evalTraces(R"(
        builtins.addErrorContext "outer context"
          (builtins.addErrorContext "inner context"
            (throw "deep"))
    )");

    int outerIdx = findTrace(frames, "outer context");
    int innerIdx = findTrace(frames, "inner context");
    ASSERT_NE(outerIdx, -1) << "outer context not found in traces";
    ASSERT_NE(innerIdx, -1) << "inner context not found in traces";

    // Record the actual order — this is what we want to track.
    // addTrace uses push_front, and addErrorContext catches & rethrows,
    // so the outermost context ends up first in the list.
    EXPECT_LT(outerIdx, innerIdx)
        << "outer context (idx=" << outerIdx << ") should appear before inner context (idx=" << innerIdx << ")";
}

// --- Function call traces ---

TEST_F(EvalTraceTest, functionCallTypeError)
{
    auto frames = evalTraces(R"(
        let f = x: x + "not a number"; in f 42
    )");
    // This produces a type error from the + operator
    // The error message is in ErrorInfo.msg; traces may or may not be present
    // depending on the evaluation path
}

// --- Traces through let bindings ---

TEST_F(EvalTraceTest, letBindingTrace)
{
    auto frames = evalTraces(R"(
        let x = throw "in let"; in x
    )");
    // A throw inside a let binding produces a trace when the binding is forced
    EXPECT_TRUE(hasTraceContaining(frames, "throw"));
}

// --- computeTraceDisplay integration ---

TEST_F(EvalTraceTest, displayEventsFromRealError)
{
    // Evaluate something that produces traces, then feed to computeTraceDisplay
    auto info = evalErrorInfo(R"(
        builtins.addErrorContext "ctx1"
          (builtins.addErrorContext "ctx2"
            (1 + "a"))
    )");

    // Without --show-trace: should include TracePrint::Always frames
    auto eventsNoTrace = computeTraceDisplay(info.traces, false);
    bool hasCtx1 = false, hasCtx2 = false;
    for (auto & ev : eventsNoTrace) {
        if (ev.kind == TraceEvent::Print) {
            auto hint = filterANSIEscapes(ev.trace->hint.str(), true);
            if (hint.find("ctx1") != std::string::npos) hasCtx1 = true;
            if (hint.find("ctx2") != std::string::npos) hasCtx2 = true;
        }
    }
    EXPECT_TRUE(hasCtx1) << "addErrorContext 'ctx1' should appear even without --show-trace";
    EXPECT_TRUE(hasCtx2) << "addErrorContext 'ctx2' should appear even without --show-trace";

    // With --show-trace: should include everything
    auto eventsWithTrace = computeTraceDisplay(info.traces, true);
    EXPECT_GE(eventsWithTrace.size(), eventsNoTrace.size())
        << "--show-trace should show at least as many events";
}

TEST_F(EvalTraceTest, displayTruncationOnDeepTrace)
{
    // A deeply nested expression should produce enough traces to trigger truncation
    // Build: f (f (f (... (throw "deep") ...)))
    std::string expr = "throw \"deep\"";
    for (int i = 0; i < 20; i++) {
        expr = "builtins.addErrorContext \"level " + std::to_string(i) + "\" (" + expr + ")";
    }

    auto info = evalErrorInfo(expr);

    auto eventsNoTrace = computeTraceDisplay(info.traces, false);
    auto eventsWithTrace = computeTraceDisplay(info.traces, true);

    // All 20 context levels should appear with --show-trace
    for (int i = 0; i < 20; i++) {
        bool found = false;
        for (auto & ev : eventsWithTrace) {
            if (ev.kind == TraceEvent::Print) {
                auto hint = filterANSIEscapes(ev.trace->hint.str(), true);
                if (hint.find("level " + std::to_string(i)) != std::string::npos) {
                    found = true;
                    break;
                }
            }
        }
        EXPECT_TRUE(found) << "level " << i << " should appear with --show-trace";
    }
}

// --- Trace ordering with real evaluation ---

TEST_F(EvalTraceTest, traceOrderReflectsEvalSpine)
{
    auto frames = evalTraces(R"(
        builtins.addErrorContext "A"
          (builtins.addErrorContext "B"
            (builtins.addErrorContext "C"
              (throw "end")))
    )");

    int a = findTrace(frames, "A");
    int b = findTrace(frames, "B");
    int c = findTrace(frames, "C");

    // All three should be present
    ASSERT_NE(a, -1) << "context A not found";
    ASSERT_NE(b, -1) << "context B not found";
    ASSERT_NE(c, -1) << "context C not found";

    // Traces from addErrorContext use push_front on catch/rethrow,
    // so the outermost context appears first in the list.
    EXPECT_LT(a, b) << "A (outermost) should appear before B";
    EXPECT_LT(b, c) << "B should appear before C (innermost)";
}

// ---- Rendered output layout: "read from the end" ----
//
// The rendered error output should be readable from the bottom:
//   error:
//     … outer trace
//     … inner trace
//     error: actual error message
//
// This means:
// 1. The error message appears AFTER all traces
// 2. Outer traces appear BEFORE inner traces
// 3. Truncation message (if any) appears between traces and message

TEST_F(EvalTraceTest, renderedOutputMessageAfterTraces)
{
    auto info = evalErrorInfo(R"(
        builtins.addErrorContext "my context" (throw "the real error")
    )");

    auto output = renderError(info, true);

    auto posCtx = findInOutput(output, "my context");
    auto posMsg = findInOutput(output, "the real error");

    ASSERT_NE(posCtx, std::string::npos) << "context not found in output:\n" << output;
    ASSERT_NE(posMsg, std::string::npos) << "error message not found in output:\n" << output;

    EXPECT_LT(posCtx, posMsg)
        << "Trace context should appear BEFORE the error message (read from end).\nOutput:\n" << output;
}

TEST_F(EvalTraceTest, renderedOutputOuterBeforeInner)
{
    auto info = evalErrorInfo(R"(
        builtins.addErrorContext "OUTER"
          (builtins.addErrorContext "INNER"
            (1 + "a"))
    )");

    auto output = renderError(info, true);

    auto posOuter = findInOutput(output, "OUTER");
    auto posInner = findInOutput(output, "INNER");
    auto posMsg = findInOutput(output, "cannot add");

    ASSERT_NE(posOuter, std::string::npos) << "OUTER not found in output:\n" << output;
    ASSERT_NE(posInner, std::string::npos) << "INNER not found in output:\n" << output;
    ASSERT_NE(posMsg, std::string::npos) << "error msg not found in output:\n" << output;

    // Layout: OUTER ... INNER ... error message
    EXPECT_LT(posOuter, posInner)
        << "Outer trace should appear before inner trace.\nOutput:\n" << output;
    EXPECT_LT(posInner, posMsg)
        << "Inner trace should appear before error message.\nOutput:\n" << output;
}

TEST_F(EvalTraceTest, renderedOutputTruncationBeforeMessage)
{
    // addErrorContext uses TracePrint::Always so it bypasses truncation.
    // To test truncation, we need traces with TracePrint::Default.
    // Build an ErrorInfo manually with many Default traces that have positions.
    auto pos = std::make_shared<Pos>(1, 1, Pos::String{make_ref<std::string>("test.nix")});
    ErrorInfo info{
        .level = lvlError,
        .msg = HintFmt("%s", "the actual error"),
        .pos = {},
        .traces = {},
    };
    for (int i = 0; i < 10; i++) {
        info.traces.push_back(Trace{
            .pos = pos,
            .hint = HintFmt("%s", "trace " + std::to_string(i)),
            .print = TracePrint::Default,
        });
    }

    auto output = renderError(info, false);

    auto posTrunc = findInOutput(output, "stack trace truncated");
    auto posMsg = findInOutput(output, "the actual error");

    ASSERT_NE(posTrunc, std::string::npos) << "truncation message not found in output:\n" << output;
    ASSERT_NE(posMsg, std::string::npos) << "error msg not found in output:\n" << output;

    EXPECT_LT(posTrunc, posMsg)
        << "Truncation message should appear before the error message.\nOutput:\n" << output;
}

TEST_F(EvalTraceTest, renderedOutputShowTraceShowsMore)
{
    // Same as above: use TracePrint::Default traces to trigger truncation
    auto pos = std::make_shared<Pos>(1, 1, Pos::String{make_ref<std::string>("test.nix")});
    ErrorInfo info{
        .level = lvlError,
        .msg = HintFmt("%s", "the error"),
        .pos = {},
        .traces = {},
    };
    for (int i = 0; i < 10; i++) {
        info.traces.push_back(Trace{
            .pos = pos,
            .hint = HintFmt("%s", "trace " + std::to_string(i)),
            .print = TracePrint::Default,
        });
    }

    auto withoutTrace = renderError(info, false);
    auto withTrace = renderError(info, true);

    // --show-trace output should be longer (more trace frames)
    EXPECT_GT(withTrace.size(), withoutTrace.size())
        << "--show-trace should produce more output";

    // Without --show-trace: should have truncation message
    EXPECT_NE(findInOutput(withoutTrace, "stack trace truncated"), std::string::npos);

    // With --show-trace: should NOT have truncation message
    EXPECT_EQ(findInOutput(withTrace, "stack trace truncated"), std::string::npos);
}

TEST_F(EvalTraceTest, renderedOutputErrorPrefixAtTop)
{
    auto info = evalErrorInfo(R"(
        builtins.addErrorContext "ctx" (throw "boom")
    )");

    auto output = renderError(info, true);

    // The output should start with "error:"
    auto firstError = findInOutput(output, "error:");
    EXPECT_EQ(firstError, 0u)
        << "Output should start with 'error:' prefix.\nOutput:\n" << output;

    // The trace should appear after the first "error:" but the actual message
    // should appear after the traces (at a later "error:" prefix)
    auto posCtx = findInOutput(output, "ctx");
    ASSERT_NE(posCtx, std::string::npos);
    EXPECT_GT(posCtx, firstError)
        << "Traces should appear after the initial error: prefix";
}

} // namespace nix
