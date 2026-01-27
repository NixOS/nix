/**
 * Unit tests for Bindings layer system with tombstone support.
 *
 * These tests verify the semantics of tombstone values (nullptr) in the
 * Bindings layer system, which enable attribute deletion through layering.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/expr/tests/libexpr.hh"
#include "nix/expr/attr-set.hh"

namespace nix {

class BindingsTest : public LibExprTest
{};

// =============================================================================
// Bindings::get() with tombstones
// =============================================================================

TEST_F(BindingsTest, getTombstoneShadowsBaseAttr)
{
    // Create base: { a = 1; b = 2; }
    auto base = state.buildBindings(2);
    auto & v1 = base.alloc(createSymbol("a"));
    auto & v2 = base.alloc(createSymbol("b"));
    v1.mkInt(1);
    v2.mkInt(2);
    auto baseBindings = base.finish();

    // Layer tombstone for "a" on top
    auto overlay = state.buildBindings(1);
    overlay.insert(Attr(createSymbol("a"), nullptr)); // tombstone
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    // "a" should be hidden by tombstone
    ASSERT_EQ(result->get(createSymbol("a")), nullptr);
    // "b" should still be visible
    ASSERT_NE(result->get(createSymbol("b")), nullptr);
    ASSERT_THAT(*result->get(createSymbol("b"))->value, IsIntEq(2));
}

TEST_F(BindingsTest, getTombstoneNonExistent)
{
    // Create base: { a = 1; }
    auto base = state.buildBindings(1);
    auto & v1 = base.alloc(createSymbol("a"));
    v1.mkInt(1);
    auto baseBindings = base.finish();

    // Tombstone for "z" (doesn't exist in base)
    auto overlay = state.buildBindings(1);
    overlay.insert(Attr(createSymbol("z"), nullptr));
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    // "a" still visible, "z" not present
    ASSERT_NE(result->get(createSymbol("a")), nullptr);
    ASSERT_EQ(result->get(createSymbol("z")), nullptr);
}

TEST_F(BindingsTest, getNonTombstoneShadowsBase)
{
    // Create base: { a = 1; }
    auto base = state.buildBindings(1);
    auto & v1 = base.alloc(createSymbol("a"));
    v1.mkInt(1);
    auto baseBindings = base.finish();

    // Override "a" with new value
    auto overlay = state.buildBindings(1);
    auto & v2 = overlay.alloc(createSymbol("a"));
    v2.mkInt(100);
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    // "a" should have the new value
    auto attr = result->get(createSymbol("a"));
    ASSERT_NE(attr, nullptr);
    ASSERT_THAT(*attr->value, IsIntEq(100));
}

// =============================================================================
// Iterator skips tombstones (K-way merge behavior)
// =============================================================================

TEST_F(BindingsTest, iteratorSkipsTombstoneInMiddle)
{
    // Base: { a = 1; b = 2; c = 3; }
    auto base = state.buildBindings(3);
    auto & v1 = base.alloc(createSymbol("a"));
    auto & v2 = base.alloc(createSymbol("b"));
    auto & v3 = base.alloc(createSymbol("c"));
    v1.mkInt(1);
    v2.mkInt(2);
    v3.mkInt(3);
    auto baseBindings = base.finish();

    // Delete "b" with tombstone
    auto overlay = state.buildBindings(1);
    overlay.insert(Attr(createSymbol("b"), nullptr));
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    // Iterate and collect names
    std::vector<Symbol> names;
    for (const auto & attr : *result)
        names.push_back(attr.name);

    // Should only see "a" and "c", not "b"
    ASSERT_EQ(names.size(), 2u);
    ASSERT_EQ(state.symbols[names[0]], "a");
    ASSERT_EQ(state.symbols[names[1]], "c");
}

TEST_F(BindingsTest, iteratorSkipsTombstoneAtStart)
{
    // Base: { a = 1; b = 2; }
    auto base = state.buildBindings(2);
    auto & v1 = base.alloc(createSymbol("a"));
    auto & v2 = base.alloc(createSymbol("b"));
    v1.mkInt(1);
    v2.mkInt(2);
    auto baseBindings = base.finish();

    // Delete "a" (first in sorted order)
    auto overlay = state.buildBindings(1);
    overlay.insert(Attr(createSymbol("a"), nullptr));
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    std::vector<Symbol> names;
    for (const auto & attr : *result)
        names.push_back(attr.name);

    ASSERT_EQ(names.size(), 1u);
    ASSERT_EQ(state.symbols[names[0]], "b");
}

TEST_F(BindingsTest, iteratorSkipsTombstoneAtEnd)
{
    // Base: { a = 1; b = 2; }
    auto base = state.buildBindings(2);
    auto & v1 = base.alloc(createSymbol("a"));
    auto & v2 = base.alloc(createSymbol("b"));
    v1.mkInt(1);
    v2.mkInt(2);
    auto baseBindings = base.finish();

    // Delete "b" (last in sorted order)
    auto overlay = state.buildBindings(1);
    overlay.insert(Attr(createSymbol("b"), nullptr));
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    std::vector<Symbol> names;
    for (const auto & attr : *result)
        names.push_back(attr.name);

    ASSERT_EQ(names.size(), 1u);
    ASSERT_EQ(state.symbols[names[0]], "a");
}

TEST_F(BindingsTest, iteratorAllTombstoned)
{
    // Base: { a = 1; }
    auto base = state.buildBindings(1);
    auto & v1 = base.alloc(createSymbol("a"));
    v1.mkInt(1);
    auto baseBindings = base.finish();

    // Tombstone the only attribute
    auto overlay = state.buildBindings(1);
    overlay.insert(Attr(createSymbol("a"), nullptr));
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    int count = 0;
    for ([[maybe_unused]] const auto & attr : *result)
        ++count;

    ASSERT_EQ(count, 0);
}

TEST_F(BindingsTest, iteratorMultipleTombstones)
{
    // Base: { a = 1; b = 2; c = 3; d = 4; }
    auto base = state.buildBindings(4);
    auto & v1 = base.alloc(createSymbol("a"));
    auto & v2 = base.alloc(createSymbol("b"));
    auto & v3 = base.alloc(createSymbol("c"));
    auto & v4 = base.alloc(createSymbol("d"));
    v1.mkInt(1);
    v2.mkInt(2);
    v3.mkInt(3);
    v4.mkInt(4);
    auto baseBindings = base.finish();

    // Delete "a" and "c"
    auto overlay = state.buildBindings(2);
    overlay.insert(Attr(createSymbol("a"), nullptr));
    overlay.insert(Attr(createSymbol("c"), nullptr));
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    std::vector<Symbol> names;
    for (const auto & attr : *result)
        names.push_back(attr.name);

    ASSERT_EQ(names.size(), 2u);
    ASSERT_EQ(state.symbols[names[0]], "b");
    ASSERT_EQ(state.symbols[names[1]], "d");
}

// =============================================================================
// Size calculation with tombstones
// =============================================================================

TEST_F(BindingsTest, sizeTombstoneDeletesBaseAttr)
{
    // Base: { a = 1; b = 2; c = 3; }
    auto base = state.buildBindings(3);
    auto & v1 = base.alloc(createSymbol("a"));
    auto & v2 = base.alloc(createSymbol("b"));
    auto & v3 = base.alloc(createSymbol("c"));
    v1.mkInt(1);
    v2.mkInt(2);
    v3.mkInt(3);
    auto baseBindings = base.finish();
    ASSERT_EQ(baseBindings->size(), 3u);

    // Tombstone "b"
    auto overlay = state.buildBindings(1);
    overlay.insert(Attr(createSymbol("b"), nullptr));
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    // Size should be 2 (3 - 1 deleted)
    ASSERT_EQ(result->size(), 2u);
}

TEST_F(BindingsTest, sizeTombstoneNonExistentNoEffect)
{
    // Base: { a = 1; b = 2; }
    auto base = state.buildBindings(2);
    auto & v1 = base.alloc(createSymbol("a"));
    auto & v2 = base.alloc(createSymbol("b"));
    v1.mkInt(1);
    v2.mkInt(2);
    auto baseBindings = base.finish();

    // Tombstone for non-existent "z"
    auto overlay = state.buildBindings(1);
    overlay.insert(Attr(createSymbol("z"), nullptr));
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    // Size should still be 2
    ASSERT_EQ(result->size(), 2u);
}

TEST_F(BindingsTest, sizeMixedLayering)
{
    // Base: { a = 1; b = 2; c = 3; }
    auto base = state.buildBindings(3);
    auto & v1 = base.alloc(createSymbol("a"));
    auto & v2 = base.alloc(createSymbol("b"));
    auto & v3 = base.alloc(createSymbol("c"));
    v1.mkInt(1);
    v2.mkInt(2);
    v3.mkInt(3);
    auto baseBindings = base.finish();

    // Overlay: delete "b", override "c" with new value, add "d"
    auto overlay = state.buildBindings(3);
    overlay.insert(Attr(createSymbol("b"), nullptr)); // delete
    auto & v4 = overlay.alloc(createSymbol("c"));     // override
    auto & v5 = overlay.alloc(createSymbol("d"));     // new
    v4.mkInt(30);
    v5.mkInt(4);
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    // Final: { a = 1; c = 30; d = 4; } -> size 3
    ASSERT_EQ(result->size(), 3u);
}

TEST_F(BindingsTest, sizeConsistentWithIteration)
{
    // Base: { a = 1; b = 2; c = 3; d = 4; e = 5; }
    auto base = state.buildBindings(5);
    auto & v1 = base.alloc(createSymbol("a"));
    auto & v2 = base.alloc(createSymbol("b"));
    auto & v3 = base.alloc(createSymbol("c"));
    auto & v4 = base.alloc(createSymbol("d"));
    auto & v5 = base.alloc(createSymbol("e"));
    v1.mkInt(1);
    v2.mkInt(2);
    v3.mkInt(3);
    v4.mkInt(4);
    v5.mkInt(5);
    auto baseBindings = base.finish();

    // Delete "b" and "d", add "f"
    auto overlay = state.buildBindings(3);
    overlay.insert(Attr(createSymbol("b"), nullptr));
    overlay.insert(Attr(createSymbol("d"), nullptr));
    auto & v6 = overlay.alloc(createSymbol("f"));
    v6.mkInt(6);
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    // Count via iteration
    size_t iterCount = 0;
    for ([[maybe_unused]] const auto & attr : *result)
        ++iterCount;

    // size() should match iteration count
    ASSERT_EQ(result->size(), iterCount);
    ASSERT_EQ(result->size(), 4u); // a, c, e, f
}

// =============================================================================
// Multi-layer tombstone scenarios
// =============================================================================

TEST_F(BindingsTest, multiLayerTombstoneRestored)
{
    // Layer 0 (base): { a = 1; b = 2; }
    auto layer0 = state.buildBindings(2);
    auto & v1 = layer0.alloc(createSymbol("a"));
    auto & v2 = layer0.alloc(createSymbol("b"));
    v1.mkInt(1);
    v2.mkInt(2);
    auto layer0Bindings = layer0.finish();

    // Layer 1: tombstone for "a"
    auto layer1 = state.buildBindings(1);
    layer1.insert(Attr(createSymbol("a"), nullptr));
    layer1.layerOnTopOf(*layer0Bindings);
    auto layer1Bindings = layer1.finish();

    // Verify "a" is hidden at layer 1
    ASSERT_EQ(layer1Bindings->get(createSymbol("a")), nullptr);
    ASSERT_EQ(layer1Bindings->size(), 1u);

    // Layer 2: restore "a" with new value
    auto layer2 = state.buildBindings(1);
    auto & v3 = layer2.alloc(createSymbol("a"));
    v3.mkInt(100);
    layer2.layerOnTopOf(*layer1Bindings);
    auto result = layer2.finish();

    // "a" should be visible again with value 100
    auto attrA = result->get(createSymbol("a"));
    ASSERT_NE(attrA, nullptr);
    ASSERT_THAT(*attrA->value, IsIntEq(100));
    ASSERT_EQ(result->size(), 2u);
}

TEST_F(BindingsTest, multiLayerTombstonePersists)
{
    // Layer 0: { a = 1; b = 2; }
    auto layer0 = state.buildBindings(2);
    auto & v1 = layer0.alloc(createSymbol("a"));
    auto & v2 = layer0.alloc(createSymbol("b"));
    v1.mkInt(1);
    v2.mkInt(2);
    auto layer0Bindings = layer0.finish();

    // Layer 1: tombstone for "a"
    auto layer1 = state.buildBindings(1);
    layer1.insert(Attr(createSymbol("a"), nullptr));
    layer1.layerOnTopOf(*layer0Bindings);
    auto layer1Bindings = layer1.finish();

    // Layer 2: add "c" (tombstone for "a" should still apply)
    auto layer2 = state.buildBindings(1);
    auto & v3 = layer2.alloc(createSymbol("c"));
    v3.mkInt(3);
    layer2.layerOnTopOf(*layer1Bindings);
    auto result = layer2.finish();

    // "a" should still be hidden
    ASSERT_EQ(result->get(createSymbol("a")), nullptr);
    // "b" and "c" should be visible
    ASSERT_NE(result->get(createSymbol("b")), nullptr);
    ASSERT_NE(result->get(createSymbol("c")), nullptr);
    ASSERT_EQ(result->size(), 2u);

    // Verify iteration
    std::vector<std::string> names;
    for (const auto & attr : *result)
        names.push_back(std::string(state.symbols[attr.name]));

    ASSERT_EQ(names.size(), 2u);
    ASSERT_EQ(names[0], "b");
    ASSERT_EQ(names[1], "c");
}

// =============================================================================
// Edge cases
// =============================================================================

TEST_F(BindingsTest, emptyOverlayWithTombstone)
{
    // Empty base
    auto base = state.buildBindings(0);
    auto baseBindings = base.finish();
    ASSERT_EQ(baseBindings->size(), 0u);

    // Tombstone in overlay (for non-existent)
    auto overlay = state.buildBindings(1);
    overlay.insert(Attr(createSymbol("a"), nullptr));
    overlay.layerOnTopOf(*baseBindings);
    auto result = overlay.finish();

    ASSERT_EQ(result->size(), 0u);
    ASSERT_EQ(result->get(createSymbol("a")), nullptr);
}

TEST_F(BindingsTest, newValueShadowsTombstone)
{
    // Test that a real value layered on top of a tombstone "resurrects" the attr.
    // This simulates: base has "a", middle layer deletes "a", top layer adds new "a".

    // Base: { a = 1; }
    auto base = state.buildBindings(1);
    auto & v1 = base.alloc(createSymbol("a"));
    v1.mkInt(1);
    auto baseBindings = base.finish();

    // Layer 1: tombstone deletes "a"
    auto layer1 = state.buildBindings(1);
    layer1.insert(Attr(createSymbol("a"), nullptr));
    layer1.layerOnTopOf(*baseBindings);
    auto layer1Bindings = layer1.finish();

    // Verify "a" is deleted at this point
    ASSERT_EQ(layer1Bindings->get(createSymbol("a")), nullptr);
    ASSERT_EQ(layer1Bindings->size(), 0u);

    // Layer 2: new "a" shadows the tombstone
    auto layer2 = state.buildBindings(1);
    auto & v2 = layer2.alloc(createSymbol("a"));
    v2.mkInt(999);
    layer2.layerOnTopOf(*layer1Bindings);
    auto result = layer2.finish();

    // "a" is back with the new value
    auto attr = result->get(createSymbol("a"));
    ASSERT_NE(attr, nullptr);
    ASSERT_THAT(*attr->value, IsIntEq(999));
    ASSERT_EQ(result->size(), 1u);
}

} // namespace nix
