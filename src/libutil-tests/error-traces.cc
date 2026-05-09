#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/error.hh"
#include "nix/util/position.hh"
#include "nix/util/terminal.hh"

namespace nix {

namespace {

/// Helper: make a trace without a position
Trace nopos(std::string msg, TracePrint print = TracePrint::Default)
{
    return Trace{.pos = nullptr, .hint = HintFmt("%s", msg), .print = print};
}

/// Helper: make a trace that is considered to "have a position"
/// by the hasPos predicate we pass to computeTraceDisplay.
Trace withpos(std::string msg, TracePrint print = TracePrint::Default)
{
    // We use a tag object; the actual Pos content doesn't matter because
    // we supply a custom hasPos predicate in the tests.
    auto pos = std::make_shared<Pos>(1, 1, Pos::String{make_ref<std::string>("test")});
    return Trace{.pos = std::move(pos), .hint = HintFmt("%s", msg), .print = print};
}

/// Predicate matching production behavior: pos && *pos
bool prodHasPos(const Trace & t)
{
    return t.pos && *t.pos;
}

/// Extract the printed trace hints in order
std::vector<std::string> printedHints(const std::vector<TraceEvent> & events)
{
    std::vector<std::string> out;
    for (auto & e : events)
        if (e.kind == TraceEvent::Print)
            out.push_back(filterANSIEscapes(e.trace->hint.str(), true));
    return out;
}

/// Check whether the event list contains a Truncated event
bool hasTruncated(const std::vector<TraceEvent> & events)
{
    for (auto & e : events)
        if (e.kind == TraceEvent::Truncated)
            return true;
    return false;
}

/// Check whether the event list contains a DuplicatesOmitted event
bool hasDuplicatesOmitted(const std::vector<TraceEvent> & events)
{
    for (auto & e : events)
        if (e.kind == TraceEvent::DuplicatesOmitted)
            return true;
    return false;
}

} // namespace

// ---- Empty / trivial cases ----

TEST(ErrorTrace, emptyTraces)
{
    std::list<Trace> traces;
    auto events = computeTraceDisplay(traces, false, prodHasPos);
    EXPECT_TRUE(events.empty());
}

TEST(ErrorTrace, emptyHintsSkipped)
{
    std::list<Trace> traces;
    traces.push_back(Trace{.pos = nullptr, .hint = HintFmt("")});
    auto events = computeTraceDisplay(traces, false, prodHasPos);
    EXPECT_TRUE(events.empty());
}

// ---- Ordering ----

TEST(ErrorTrace, orderPreserved)
{
    std::list<Trace> traces;
    traces.push_back(nopos("first"));
    traces.push_back(nopos("second"));
    traces.push_back(nopos("third"));

    auto hints = printedHints(computeTraceDisplay(traces, true, prodHasPos));
    ASSERT_EQ(hints.size(), 3u);
    EXPECT_EQ(hints[0], "first");
    EXPECT_EQ(hints[1], "second");
    EXPECT_EQ(hints[2], "third");
}

TEST(ErrorTrace, orderPreservedWithPositions)
{
    std::list<Trace> traces;
    traces.push_back(withpos("A"));
    traces.push_back(nopos("B"));
    traces.push_back(withpos("C"));

    auto hints = printedHints(computeTraceDisplay(traces, true, prodHasPos));
    ASSERT_EQ(hints.size(), 3u);
    EXPECT_EQ(hints[0], "A");
    EXPECT_EQ(hints[1], "B");
    EXPECT_EQ(hints[2], "C");
}

// ---- Truncation ----

TEST(ErrorTrace, noTruncationWithShowTrace)
{
    std::list<Trace> traces;
    for (int i = 0; i < 20; i++)
        traces.push_back(withpos("trace " + std::to_string(i)));

    auto events = computeTraceDisplay(traces, true, prodHasPos);
    EXPECT_FALSE(hasTruncated(events));
    EXPECT_EQ(printedHints(events).size(), 20u);
}

TEST(ErrorTrace, noTruncationUnderLimit)
{
    // 3 traces with positions → count reaches 3, but truncation triggers at > 3
    std::list<Trace> traces;
    for (int i = 0; i < 3; i++)
        traces.push_back(withpos("trace " + std::to_string(i)));

    auto events = computeTraceDisplay(traces, false, prodHasPos);
    EXPECT_FALSE(hasTruncated(events));
    EXPECT_EQ(printedHints(events).size(), 3u);
}

TEST(ErrorTrace, truncationAtLimit)
{
    // 6 traces with positions. Truncation keeps the inner (last) 3-4 traces
    // and drops the outer (first) ones. The Truncated event appears first.
    std::list<Trace> traces;
    for (int i = 0; i < 6; i++)
        traces.push_back(withpos("trace " + std::to_string(i)));

    auto events = computeTraceDisplay(traces, false, prodHasPos);
    EXPECT_TRUE(hasTruncated(events));
    auto hints = printedHints(events);
    // Inner traces (near the error) should be kept
    EXPECT_THAT(hints, ::testing::Contains("trace 5"));
    // Outer traces should be truncated
    EXPECT_THAT(hints, ::testing::Not(::testing::Contains("trace 0")));
    // Truncated event should be first
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.front().kind, TraceEvent::Truncated);
}

TEST(ErrorTrace, tracesWithoutPositionsDontCountTowardLimit)
{
    // Many traces without positions should NOT trigger truncation
    std::list<Trace> traces;
    for (int i = 0; i < 20; i++)
        traces.push_back(nopos("trace " + std::to_string(i)));

    auto events = computeTraceDisplay(traces, false, prodHasPos);
    EXPECT_FALSE(hasTruncated(events));
    EXPECT_EQ(printedHints(events).size(), 20u);
}

// ---- TracePrint::Always bypasses truncation ----

TEST(ErrorTrace, alwaysPrintBypassesTruncation)
{
    // TracePrint::Always traces in the truncated (outer) region still appear
    std::list<Trace> traces;
    traces.push_back(nopos("always at top", TracePrint::Always));
    for (int i = 0; i < 10; i++)
        traces.push_back(withpos("default " + std::to_string(i)));

    auto events = computeTraceDisplay(traces, false, prodHasPos);
    EXPECT_TRUE(hasTruncated(events));
    auto hints = printedHints(events);
    EXPECT_THAT(hints, ::testing::Contains("always at top"));
}

TEST(ErrorTrace, alwaysPrintPreservesOrderAmongOtherAlways)
{
    // Two Always traces in the outer (truncated) region preserve their relative order
    std::list<Trace> traces;
    traces.push_back(nopos("ctx A", TracePrint::Always));
    traces.push_back(nopos("ctx B", TracePrint::Always));
    for (int i = 0; i < 10; i++)
        traces.push_back(withpos("filler " + std::to_string(i)));

    auto events = computeTraceDisplay(traces, false, prodHasPos);
    auto hints = printedHints(events);
    auto posA = std::find(hints.begin(), hints.end(), "ctx A");
    auto posB = std::find(hints.begin(), hints.end(), "ctx B");
    ASSERT_NE(posA, hints.end());
    ASSERT_NE(posB, hints.end());
    EXPECT_LT(std::distance(hints.begin(), posA), std::distance(hints.begin(), posB));
}

// ---- Deduplication ----

TEST(ErrorTrace, fewDuplicatesNotOmitted)
{
    std::list<Trace> traces;
    traces.push_back(nopos("unique"));
    // 3 duplicates of "unique" → ≤5, so they are printed individually
    for (int i = 0; i < 3; i++)
        traces.push_back(nopos("unique"));

    auto events = computeTraceDisplay(traces, true, prodHasPos);
    EXPECT_FALSE(hasDuplicatesOmitted(events));
    // 1 original + 3 duplicates = 4 printed
    EXPECT_EQ(printedHints(events).size(), 4u);
}

TEST(ErrorTrace, manyDuplicatesOmitted)
{
    std::list<Trace> traces;
    traces.push_back(nopos("recursive"));
    for (int i = 0; i < 10; i++)
        traces.push_back(nopos("recursive"));

    auto events = computeTraceDisplay(traces, true, prodHasPos);
    EXPECT_TRUE(hasDuplicatesOmitted(events));
    // Only 1 printed (the first unique one), rest omitted
    EXPECT_EQ(printedHints(events).size(), 1u);
    // The omitted count should be 10
    for (auto & e : events) {
        if (e.kind == TraceEvent::DuplicatesOmitted) {
            EXPECT_EQ(e.count, 10u);
        }
    }
}

TEST(ErrorTrace, mutualRecursionDedupResets)
{
    // Pattern: 1×A, 9×A(dup), 1×B, 9×B(dup), 1×A, 9×A(dup)
    // After dedup of the first A block, tracesSeen is cleared,
    // so the second A block gets its own dedup chunk.
    std::list<Trace> traces;
    for (int i = 0; i < 10; i++)
        traces.push_back(nopos("A"));
    for (int i = 0; i < 10; i++)
        traces.push_back(nopos("B"));
    for (int i = 0; i < 10; i++)
        traces.push_back(nopos("A"));

    auto events = computeTraceDisplay(traces, true, prodHasPos);

    // Should see: Print(A), DuplicatesOmitted(9), Print(B), DuplicatesOmitted(9), Print(A), DuplicatesOmitted(9)
    auto hints = printedHints(events);
    EXPECT_EQ(hints.size(), 3u);
    EXPECT_EQ(hints[0], "A");
    EXPECT_EQ(hints[1], "B");
    EXPECT_EQ(hints[2], "A");

    size_t dedupCount = 0;
    for (auto & e : events)
        if (e.kind == TraceEvent::DuplicatesOmitted)
            dedupCount++;
    EXPECT_EQ(dedupCount, 3u);
}

// ---- Combined behaviors ----

TEST(ErrorTrace, truncationAndDeduplicationInteract)
{
    // With showTrace=false, outer traces are truncated and inner traces kept.
    std::list<Trace> traces;
    // 5 outer traces with positions (will be truncated)
    for (int i = 0; i < 5; i++)
        traces.push_back(withpos("head " + std::to_string(i)));
    // Then many without positions (don't count toward limit)
    for (int i = 0; i < 10; i++)
        traces.push_back(nopos("repeated"));
    // 2 inner traces with positions (should be kept)
    traces.push_back(withpos("tail A"));
    traces.push_back(withpos("tail B"));

    auto events = computeTraceDisplay(traces, false, prodHasPos);
    auto hints = printedHints(events);
    // Inner traces should be kept
    EXPECT_THAT(hints, ::testing::Contains("tail A"));
    EXPECT_THAT(hints, ::testing::Contains("tail B"));
}

} // namespace nix
