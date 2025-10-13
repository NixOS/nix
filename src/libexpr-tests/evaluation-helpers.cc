#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/expr/evaluation-helpers.hh"
#include "nix/expr/interpreter-object.hh"
#include "nix/expr/interpreter.hh"
#include "nix/expr/tests/libexpr.hh"
#include "nix/util/tests/gmock-matchers.hh"

namespace nix::expr::helpers {

class EvaluatorHelpersTest : public LibExprTest
{
protected:
    EvaluatorHelpersTest()
        : LibExprTest()
        , evaluator(statePtr)
    {
    }

    Interpreter evaluator;

    Value * makeAttrs(const std::map<std::string, std::string> & attrs)
    {
        auto v = state.allocValue();
        auto bindings = state.buildBindings(attrs.size());
        for (auto & [name, value] : attrs) {
            auto vStr = state.allocValue();
            vStr->mkString(value);
            bindings.insert(state.symbols.create(name), vStr);
        }
        v->mkAttrs(bindings.finish());
        return v;
    }

    Value * makeString(const std::string & s)
    {
        auto v = state.allocValue();
        v->mkString(s);
        return v;
    }
};

TEST_F(EvaluatorHelpersTest, isDerivation_ReturnsTrueForDerivation)
{
    // Create an attrset with type = "derivation"
    auto v = makeAttrs({{"type", "derivation"}});
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_TRUE(isDerivation(*obj));
}

TEST_F(EvaluatorHelpersTest, isDerivation_ReturnsFalseForNonDerivation)
{
    // Create an attrset with type = "package"
    auto v = makeAttrs({{"type", "package"}});
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_FALSE(isDerivation(*obj));
}

TEST_F(EvaluatorHelpersTest, isDerivation_ReturnsFalseWhenTypeAttributeMissing)
{
    // Create an attrset without a type attribute
    auto v = makeAttrs({{"name", "test"}});
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_FALSE(isDerivation(*obj));
}

TEST_F(EvaluatorHelpersTest, isDerivation_ReturnsFalseWhenNotAnAttrSet)
{
    // Create a string value instead of an attrset
    auto v = makeString("not an attrset");
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_FALSE(isDerivation(*obj));
}

TEST_F(EvaluatorHelpersTest, isDerivation_ReturnsFalseWhenTypeIsNotString)
{
    // Create an attrset where type is not a string (e.g., a number)
    auto v = state.allocValue();
    auto bindings = state.buildBindings(1);
    auto vNum = state.allocValue();
    vNum->mkInt(42);
    bindings.insert(state.symbols.create("type"), vNum);
    v->mkAttrs(bindings.finish());

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_FALSE(isDerivation(*obj));
}

TEST_F(EvaluatorHelpersTest, getListOfStringsNoCtx_ReturnsListOfStrings)
{
    // Create a list of strings
    auto vFoo = state.allocValue();
    vFoo->mkString("foo");
    auto vBar = state.allocValue();
    vBar->mkString("bar");
    auto vBaz = state.allocValue();
    vBaz->mkString("baz");

    auto list = state.buildList(3);
    list.elems[0] = vFoo;
    list.elems[1] = vBar;
    list.elems[2] = vBaz;

    auto v = state.allocValue();
    v->mkList(list);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = obj->getListOfStringsNoCtx();
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "foo");
    EXPECT_EQ(result[1], "bar");
    EXPECT_EQ(result[2], "baz");
}

TEST_F(EvaluatorHelpersTest, getListOfStringsNoCtx_ThrowsWhenNotAList)
{
    auto v = makeString("not a list");
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_THROW(obj->getListOfStringsNoCtx(), Error);
}

TEST_F(EvaluatorHelpersTest, getListOfStringsNoCtx_ThrowsWhenListContainsNonString)
{
    // Create a list with a non-string element
    auto vFoo = state.allocValue();
    vFoo->mkString("foo");
    auto vNum = state.allocValue();
    vNum->mkInt(42);

    auto list = state.buildList(2);
    list.elems[0] = vFoo;
    list.elems[1] = vNum;

    auto v = state.allocValue();
    v->mkList(list);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    try {
        obj->getListOfStringsNoCtx();
        FAIL() << "Expected Error to be thrown";
    } catch (const Error & e) {
        // Verify the error message contains the index
        EXPECT_THAT(e.what(), ::testing::HasSubstr("index 1"));
    }
}

TEST_F(EvaluatorHelpersTest, getListOfStringsNoCtx_ReturnsEmptyListForEmptyList)
{
    auto list = state.buildList(0);
    auto v = state.allocValue();
    v->mkList(list);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = obj->getListOfStringsNoCtx();
    EXPECT_EQ(result.size(), 0);
}

// Tests for getBool primitive
TEST_F(EvaluatorHelpersTest, getBool_ReturnsTrue)
{
    auto v = state.allocValue();
    v->mkBool(true);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_TRUE(obj->getBool(""));
}

TEST_F(EvaluatorHelpersTest, getBool_ReturnsFalse)
{
    auto v = state.allocValue();
    v->mkBool(false);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_FALSE(obj->getBool(""));
}

TEST_F(EvaluatorHelpersTest, getBool_ThrowsWhenNotABool)
{
    auto v = makeString("not a bool");
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_THROW(obj->getBool(""), Error);
}

TEST_F(EvaluatorHelpersTest, getBool_IncludesErrorContext)
{
    auto v = state.allocValue();
    v->mkInt(42);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    try {
        obj->getBool("while checking some_bool_context");
        FAIL() << "Expected Error to be thrown";
    } catch (const Error & e) {
        EXPECT_THAT(e.what(), ::testing::HasSubstr("some_bool_context"));
    }
}

// Tests for getInt primitive
TEST_F(EvaluatorHelpersTest, getInt_ReturnsInteger)
{
    auto v = state.allocValue();
    v->mkInt(42);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_EQ(obj->getInt("").value, 42);
}

TEST_F(EvaluatorHelpersTest, getInt_ReturnsNegativeInteger)
{
    auto v = state.allocValue();
    v->mkInt(-123);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_EQ(obj->getInt("").value, -123);
}

TEST_F(EvaluatorHelpersTest, getInt_ThrowsWhenNotAnInt)
{
    auto v = makeString("some_string");
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_THROW(obj->getInt(""), Error);
}

TEST_F(EvaluatorHelpersTest, getInt_IncludesErrorContext)
{
    auto v = state.allocValue();
    v->mkBool(true);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    try {
        obj->getInt("while evaluating some_int_context");
        FAIL() << "Expected Error to be thrown";
    } catch (const Error & e) {
        EXPECT_THAT(e.what(), ::testing::HasSubstr("some_int_context"));
    }
}

// Tests for findAlongAttrPath helper
TEST_F(EvaluatorHelpersTest, findAlongAttrPath_EmptyPath)
{
    // Empty path should return the object itself
    auto v = makeAttrs({{"foo", "bar"}});
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = findAlongAttrPath(*obj, {});

    EXPECT_TRUE(result) << "Empty path should succeed";
    EXPECT_EQ(result->get(), obj.get()) << "Empty path should return the same object";
}

TEST_F(EvaluatorHelpersTest, findAlongAttrPath_SingleAttribute)
{
    // Navigate to a single attribute
    auto v = state.allocValue();
    auto bindings = state.buildBindings(1);
    auto vNested = state.allocValue();
    vNested->mkString("value");
    bindings.insert(state.symbols.create("foo"), vNested);
    v->mkAttrs(bindings.finish());

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = findAlongAttrPath(*obj, {"foo"});

    ASSERT_TRUE(result) << "Should find attribute 'foo'";
    auto str = (*result)->getStringIgnoreContext();
    EXPECT_EQ(str, "value");
}

TEST_F(EvaluatorHelpersTest, findAlongAttrPath_NestedAttributes)
{
    // Navigate through nested attributes
    auto expr = state.parseExprFromString("{ a = { b = { c = \"deep\"; }; }; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = findAlongAttrPath(*obj, {"a", "b", "c"});

    ASSERT_TRUE(result) << "Should find nested attribute 'a.b.c'";
    auto str = (*result)->getStringIgnoreContext();
    EXPECT_EQ(str, "deep");
}

TEST_F(EvaluatorHelpersTest, findAlongAttrPath_AttributeNotFound)
{
    // Attribute doesn't exist
    auto v = makeAttrs({{"foo", "bar"}});
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = findAlongAttrPath(*obj, {"missing"});

    EXPECT_FALSE(result) << "Should fail when attribute not found";
}

TEST_F(EvaluatorHelpersTest, findAlongAttrPath_MidPathNotFound)
{
    // Middle attribute in path doesn't exist
    auto expr = state.parseExprFromString("{ a = { b = \"value\"; }; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = findAlongAttrPath(*obj, {"a", "missing", "c"});

    EXPECT_FALSE(result) << "Should fail when middle attribute not found";
}

TEST_F(EvaluatorHelpersTest, findAlongAttrPath_NotAnAttrSet)
{
    // Try to navigate through a non-attrset - throws when trying to generate suggestions
    auto v = makeString("not an attrset");
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_THROW(findAlongAttrPath(*obj, {"foo"}), Error)
        << "Should throw when trying to get attribute from non-attrset";
}

TEST_F(EvaluatorHelpersTest, findAlongAttrPath_MidPathNotAnAttrSet)
{
    // Middle element in path is not an attrset - throws when trying to generate suggestions
    auto expr = state.parseExprFromString("{ a = \"string\"; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_THROW(findAlongAttrPath(*obj, {"a", "b"}), Error) << "Should throw when middle element is not an attrset";
}

// Tests for suggestions in findAlongAttrPath
TEST_F(EvaluatorHelpersTest, findAlongAttrPath_SuggestsCloseMatch)
{
    // Typo: "fo" instead of "foo"
    auto expr = state.parseExprFromString("{ foo = \"value\"; bar = \"other\"; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = findAlongAttrPath(*obj, {"fo"});

    EXPECT_FALSE(result) << "Should fail for typo";
    auto suggestions = result.getSuggestions();
    EXPECT_FALSE(suggestions.suggestions.empty()) << "Should provide suggestions";

    // Check if "foo" is in the suggestions
    bool foundFoo = false;
    for (const auto & suggestion : suggestions.suggestions) {
        if (suggestion.suggestion == "foo") {
            foundFoo = true;
            break;
        }
    }
    EXPECT_TRUE(foundFoo) << "Should suggest 'foo' for typo 'fo'";
}

TEST_F(EvaluatorHelpersTest, findAlongAttrPath_SuggestsForNestedTypo)
{
    // Typo in nested path: "a.b.bz" instead of "a.b.baz"
    auto expr =
        state.parseExprFromString("{ a = { b = { baz = \"value\"; bar = \"other\"; }; }; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);

    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = findAlongAttrPath(*obj, {"a", "b", "bz"});

    EXPECT_FALSE(result) << "Should fail for nested typo";
    auto suggestions = result.getSuggestions();
    EXPECT_FALSE(suggestions.suggestions.empty()) << "Should provide suggestions for nested attribute";

    // Check if "baz" or "bar" is in the suggestions
    bool foundMatch = false;
    for (const auto & suggestion : suggestions.suggestions) {
        if (suggestion.suggestion == "baz" || suggestion.suggestion == "bar") {
            foundMatch = true;
            break;
        }
    }
    EXPECT_TRUE(foundMatch) << "Should suggest 'baz' or 'bar' for nested typo 'bz'";
}

TEST_F(EvaluatorHelpersTest, findAlongAttrPath_ThrowsForNonAttrset)
{
    // Throws when trying to get suggestions for non-attrset (matches AttrCursor::getSuggestionsForAttr behavior)
    auto v = makeString("not an attrset");
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    EXPECT_THROW(findAlongAttrPath(*obj, {"foo"}), Error)
        << "Should throw when trying to get suggestions for non-attrset";
}

// Tests for getDerivationOutputs helper
TEST_F(EvaluatorHelpersTest, getDerivationOutputs_ReturnsDefaultOut)
{
    // Minimal derivation with no metadata should default to "out"
    auto expr = state.parseExprFromString(
        "derivation { name = \"test\"; system = \"x86_64-linux\"; builder = \"/bin/sh\"; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto outputs = getDerivationOutputs(*obj);

    EXPECT_EQ(outputs.size(), 1);
    EXPECT_TRUE(outputs.count("out"));
}

TEST_F(EvaluatorHelpersTest, getDerivationOutputs_ReturnsOutputsToInstallFromMeta)
{
    // Derivation with meta.outputsToInstall
    auto expr = state.parseExprFromString(
        R"(
        (derivation {
            name = "test";
            system = "x86_64-linux";
            builder = "/bin/sh";
        }) // { meta = { outputsToInstall = [ "bin" "dev" ]; }; }
    )",
        state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto outputs = getDerivationOutputs(*obj);

    EXPECT_EQ(outputs.size(), 2);
    EXPECT_TRUE(outputs.count("bin"));
    EXPECT_TRUE(outputs.count("dev"));
}

TEST_F(EvaluatorHelpersTest, getDerivationOutputs_ReturnsOutputNameWhenOutputSpecified)
{
    // Derivation with outputSpecified = true and outputName
    auto expr = state.parseExprFromString(
        R"(
        (derivation {
            name = "test";
            system = "x86_64-linux";
            builder = "/bin/sh";
        }) // { outputSpecified = true; outputName = "custom"; }
    )",
        state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto outputs = getDerivationOutputs(*obj);

    EXPECT_EQ(outputs.size(), 1);
    EXPECT_TRUE(outputs.count("custom"));
}

TEST_F(EvaluatorHelpersTest, getDerivationOutputs_PrefersOutputSpecifiedOverMeta)
{
    // outputSpecified takes precedence over meta.outputsToInstall
    auto expr = state.parseExprFromString(
        R"(
        (derivation {
            name = "test";
            system = "x86_64-linux";
            builder = "/bin/sh";
        }) // {
            outputSpecified = true;
            outputName = "preferred";
            meta = { outputsToInstall = [ "should-be-ignored" ]; };
        }
    )",
        state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto outputs = getDerivationOutputs(*obj);

    EXPECT_EQ(outputs.size(), 1);
    EXPECT_TRUE(outputs.count("preferred"));
    EXPECT_FALSE(outputs.count("should-be-ignored"));
}

TEST_F(EvaluatorHelpersTest, getDerivationOutputs_OutputSpecifiedFalseIgnoresMeta)
{
    // If outputSpecified exists but is false, meta is ignored (uses else if in original)
    auto expr = state.parseExprFromString(
        R"(
        (derivation {
            name = "test";
            system = "x86_64-linux";
            builder = "/bin/sh";
        }) // {
            outputSpecified = false;
            meta = { outputsToInstall = [ "should-be-ignored" ]; };
        }
    )",
        state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto outputs = getDerivationOutputs(*obj);

    // Should default to "out", not use meta
    EXPECT_EQ(outputs.size(), 1);
    EXPECT_TRUE(outputs.count("out"));
    EXPECT_FALSE(outputs.count("should-be-ignored"));
}

TEST_F(EvaluatorHelpersTest, getDerivationOutputs_OutputSpecifiedTrueWithoutOutputNameDefaultsToOut)
{
    // If outputSpecified=true but outputName is missing, should default to "out"
    auto expr = state.parseExprFromString(
        R"(
        (derivation {
            name = "test";
            system = "x86_64-linux";
            builder = "/bin/sh";
        }) // {
            outputSpecified = true;
            meta = { outputsToInstall = [ "should-be-ignored" ]; };
        }
    )",
        state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto outputs = getDerivationOutputs(*obj);

    EXPECT_EQ(outputs.size(), 1);
    EXPECT_TRUE(outputs.count("out"));
    EXPECT_FALSE(outputs.count("should-be-ignored"));
}

// Tests for trySinglePathToDerivedPath helper
TEST_F(EvaluatorHelpersTest, trySinglePathToDerivedPath_ReturnsNulloptForNonPathNonString)
{
    // Create an integer value - should return nullopt
    auto v = state.allocValue();
    v->mkInt(42);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = trySinglePathToDerivedPath(evaluator, *obj, "test context");

    EXPECT_FALSE(result.has_value());
}

TEST_F(EvaluatorHelpersTest, trySinglePathToDerivedPath_ReturnsNulloptForAttrSet)
{
    // Create an attrset - should return nullopt
    auto v = makeAttrs({{"foo", "bar"}});
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = trySinglePathToDerivedPath(evaluator, *obj, "test context");

    EXPECT_FALSE(result.has_value());
}

TEST_F(EvaluatorHelpersTest, trySinglePathToDerivedPath_HandlesPath)
{
    // Create a temporary file to use as a path
    AutoDelete tmpDir(createTempDir(), true);
    auto testFile = tmpDir.path() + "/test.txt";
    writeFile(testFile, "test content");

    // Create a path value
    auto v = state.allocValue();
    v->mkPath(state.rootPath(CanonPath(testFile)));
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = trySinglePathToDerivedPath(evaluator, *obj, "");

    ASSERT_TRUE(result.has_value());
    // Should be DerivedPath::Opaque
    auto * opaque = std::get_if<DerivedPath::Opaque>(&*result);
    ASSERT_TRUE(opaque != nullptr) << "Should return DerivedPath::Opaque for path";
}

TEST_F(EvaluatorHelpersTest, trySinglePathToDerivedPath_HandlesStringWithOpaqueContext)
{
    // Create a string with opaque store path context
    auto v = state.allocValue();
    NixStringContext context;
    context.insert(
        NixStringContextElem::Opaque{
            .path = state.store->parseStorePath("/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-example")});
    v->mkString("/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-example", context);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = trySinglePathToDerivedPath(evaluator, *obj, "");

    ASSERT_TRUE(result.has_value());
    // Should be DerivedPath::Opaque (from SingleDerivedPath::Opaque)
    auto * opaque = std::get_if<DerivedPath::Opaque>(&*result);
    ASSERT_TRUE(opaque != nullptr) << "Should return DerivedPath::Opaque for string with opaque context";
}

TEST_F(EvaluatorHelpersTest, trySinglePathToDerivedPath_HandlesStringWithBuiltContext)
{
    // Create a string with built context (derivation output reference)
    auto v = state.allocValue();
    NixStringContext context;
    context.insert(
        NixStringContextElem::Built{
            .drvPath = make_ref<SingleDerivedPath>(SingleDerivedPath::Opaque{
                .path = state.store->parseStorePath("/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-example.drv")}),
            .output = "out"});
    v->mkString("/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-example", context);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = trySinglePathToDerivedPath(evaluator, *obj, "");

    ASSERT_TRUE(result.has_value());
    // Should be DerivedPath::Built (from SingleDerivedPath::Built)
    auto * built = std::get_if<DerivedPath::Built>(&*result);
    ASSERT_TRUE(built != nullptr) << "Should return DerivedPath::Built for string with built context";
    // Check that outputs contains "out"
    auto * names = std::get_if<OutputsSpec::Names>(&built->outputs.raw);
    ASSERT_TRUE(names != nullptr) << "Expected OutputsSpec::Names";
    EXPECT_EQ(names->size(), 1);
    EXPECT_TRUE(names->contains("out"));
}

TEST_F(EvaluatorHelpersTest, trySinglePathToDerivedPath_ThrowsForStringWithMultipleContexts)
{
    // Create a string with multiple context elements - should throw
    auto v = state.allocValue();
    NixStringContext context;
    context.insert(
        NixStringContextElem::Opaque{
            .path = state.store->parseStorePath("/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-first")});
    context.insert(
        NixStringContextElem::Opaque{
            .path = state.store->parseStorePath("/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-second")});
    v->mkString("test string", context);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    try {
        trySinglePathToDerivedPath(evaluator, *obj, "test context");
        FAIL() << "Expected Error to be thrown for string with multiple contexts";
    } catch (const Error & e) {
        EXPECT_THAT(e.what(), nix::testing::HasSubstrIgnoreANSIMatcher("2 entries"));
        EXPECT_THAT(e.what(), nix::testing::HasSubstrIgnoreANSIMatcher("test context"));
    }
}

TEST_F(EvaluatorHelpersTest, trySinglePathToDerivedPath_ThrowsForStringWithNoContext)
{
    // Create a string with no context - should throw
    auto v = state.allocValue();
    v->mkString("plain string without context");
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    try {
        trySinglePathToDerivedPath(evaluator, *obj, "test context");
        FAIL() << "Expected Error to be thrown for string with no context";
    } catch (const Error & e) {
        EXPECT_THAT(e.what(), nix::testing::HasSubstrIgnoreANSIMatcher("0 entries"));
        EXPECT_THAT(e.what(), nix::testing::HasSubstrIgnoreANSIMatcher("test context"));
    }
}

TEST_F(EvaluatorHelpersTest, trySinglePathToDerivedPath_ThrowsForStringWithDrvDeepContext)
{
    // Create a string with DrvDeep context - should throw (not supported)
    auto v = state.allocValue();
    NixStringContext context;
    context.insert(
        NixStringContextElem::DrvDeep{
            .drvPath = state.store->parseStorePath("/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-example.drv")});
    v->mkString("test", context);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    try {
        trySinglePathToDerivedPath(evaluator, *obj, "test context");
        FAIL() << "Expected Error to be thrown for DrvDeep context";
    } catch (const Error & e) {
        EXPECT_THAT(e.what(), nix::testing::HasSubstrIgnoreANSIMatcher("complete source and binary closure"));
        EXPECT_THAT(e.what(), nix::testing::HasSubstrIgnoreANSIMatcher("test context"));
    }
}

TEST_F(EvaluatorHelpersTest, trySinglePathToDerivedPath_ErrorContextEmptyOk)
{
    // Verify that empty errorCtx works (doesn't include ": " in error message)
    auto v = state.allocValue();
    v->mkString("plain string");
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    try {
        trySinglePathToDerivedPath(evaluator, *obj, "");
        FAIL() << "Expected Error to be thrown";
    } catch (const Error & e) {
        // Error should not end with ": " when errorCtx is empty
        std::string msg = e.what();
        EXPECT_FALSE(msg.ends_with(": ")) << "Error message should not end with ': ' when errorCtx is empty";
    }
}

// Tests for tryAttrPaths

TEST_F(EvaluatorHelpersTest, tryAttrPaths_FindsFirstPath)
{
    auto expr = state.parseExprFromString("{ a = 1; b = 2; c = 3; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = expr::helpers::tryAttrPaths(*obj, {"a"}, state);
    ASSERT_TRUE(result) << "Expected to find attribute 'a'";

    auto [foundObj, path] = *result;
    EXPECT_EQ(path, "a") << "Expected path to be 'a'";
    EXPECT_EQ(foundObj->getInt("while getting int").value, 1) << "Expected value to be 1";
}

TEST_F(EvaluatorHelpersTest, tryAttrPaths_TriesMultiplePaths)
{
    auto expr = state.parseExprFromString("{ a = { b = 42; }; c = 99; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    // First path doesn't exist, second path does
    auto result = expr::helpers::tryAttrPaths(*obj, {"x.y", "a.b"}, state);
    ASSERT_TRUE(result) << "Expected to find attribute 'a.b' after 'x.y' fails";

    auto [foundObj, path] = *result;
    EXPECT_EQ(path, "a.b") << "Expected second path 'a.b' to succeed";
    EXPECT_EQ(foundObj->getInt("while getting int").value, 42) << "Expected value to be 42";
}

TEST_F(EvaluatorHelpersTest, tryAttrPaths_ReturnsFirstSuccess)
{
    auto expr = state.parseExprFromString("{ a = 1; b = 2; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    // Both paths exist, should return first
    auto result = expr::helpers::tryAttrPaths(*obj, {"a", "b"}, state);
    ASSERT_TRUE(result) << "Expected to find first path 'a'";

    auto [foundObj, path] = *result;
    EXPECT_EQ(path, "a") << "Expected first path 'a' to be returned, not 'b'";
    EXPECT_EQ(foundObj->getInt("while getting int").value, 1) << "Expected value from first path to be 1";
}

TEST_F(EvaluatorHelpersTest, tryAttrPaths_FailsWhenNoneFound)
{
    auto expr = state.parseExprFromString("{ a = 1; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = expr::helpers::tryAttrPaths(*obj, {"x", "y", "z"}, state);
    EXPECT_FALSE(result) << "Expected all paths to fail when none exist";
}

TEST_F(EvaluatorHelpersTest, tryAttrPaths_AccumulatesSuggestions)
{
    auto expr = state.parseExprFromString("{ abc = 1; abd = 2; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    // Try non-existent paths that should generate suggestions
    auto result = expr::helpers::tryAttrPaths(*obj, {"abx", "aby"}, state);
    ASSERT_FALSE(result) << "Expected both paths to fail";

    // Suggestions should include close matches like "abc" or "abd"
    auto suggestions = result.getSuggestions();
    EXPECT_GT(suggestions.suggestions.size(), 0) << "Expected suggestions for similar attribute names";
}

TEST_F(EvaluatorHelpersTest, tryAttrPaths_HandlesNestedPaths)
{
    auto expr = state.parseExprFromString("{ a = { b = { c = 123; }; }; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = expr::helpers::tryAttrPaths(*obj, {"a.b.c"}, state);
    ASSERT_TRUE(result) << "Expected to navigate nested path 'a.b.c'";

    auto [foundObj, path] = *result;
    EXPECT_EQ(path, "a.b.c") << "Expected full nested path";
    EXPECT_EQ(foundObj->getInt("while getting int").value, 123) << "Expected deeply nested value to be 123";
}

TEST_F(EvaluatorHelpersTest, tryAttrPaths_EmptyPathList)
{
    auto expr = state.parseExprFromString("{ a = 1; }", state.rootPath("."));
    auto v = state.allocValue();
    state.eval(expr, *v);
    auto obj = std::make_shared<InterpreterObject>(state, allocRootValue(v));

    auto result = expr::helpers::tryAttrPaths(*obj, {}, state);
    EXPECT_FALSE(result) << "Expected empty path list to fail";
}

} // namespace nix::expr::helpers