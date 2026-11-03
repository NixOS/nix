#include "nix/expr/tests/libexpr.hh"
#include "nix/expr/parser.hh"
#include "nix/expr/parser-state.hh"
#include "nix/util/finally.hh"

namespace nix {

template<ParserBackend Backend>
class ParserTest : public LibExprTest
{
protected:
    template<bool Repl>
    auto parse(
        std::string source,
        const SourcePath & basePath,
        const std::shared_ptr<StaticEnv> & staticEnv,
        typename Backend::traits_type::allocator_type allocator = {})
    {
        auto original = make_ref<std::string>(source);
        source.append("\0\0", 2);
        DocCommentMap docComments;
        LexerState lexerState{
            .positionToDocComment = docComments,
            .positions = state.positions,
            .origin = state.positions.addOrigin(Pos::String{.source = original}, source.size()),
        };
        ParserState parserState{
            .lexerState = lexerState,
            .exprs = state.mem.exprs,
            .symbols = state.symbols,
            .positions = state.positions,
            .basePath = basePath,
            .origin = lexerState.origin,
            .rootFS = state.rootFS,
            .settings = evalSettings,
        };
        auto input = std::span(source.data(), source.size());
        auto backend = std::make_obj_using_allocator<Backend>(allocator);
        auto result = [&] {
            if constexpr (Repl)
                return backend.parseReplBindings(input, parserState);
            else
                return backend.parseExpr(input, parserState);
        }();
        if (result)
            result->root().bindVars(state, staticEnv);
        return result;
    }

    Expr * parseExprFromString(std::string source, const SourcePath & basePath)
    {
        auto result = parse<false>(std::move(source), basePath, state.staticBaseEnv);
        if (!result)
            throw std::move(result.error());
        return &result->root();
    }

    ExprAttrs *
    parseReplBindings(std::string source, const SourcePath & basePath, const std::shared_ptr<StaticEnv> & staticEnv)
    {
        auto result = parse<true>(std::move(source), basePath, staticEnv);
        if (!result)
            throw std::move(result.error());
        return &result->root();
    }

    Value eval(std::string source)
    {
        Value value;
        auto expression = parseExprFromString(std::move(source), state.rootPath(CanonPath::root));
        state.eval(expression, value);
        state.forceValue(value, noPos);
        return value;
    }
};

using ParserBackends = ::testing::Types<BisonParserBackend>;
TYPED_TEST_SUITE(ParserTest, ParserBackends);

struct MissingBindingsTraits
{
    using expression_type = ParsedAst<Expr>;
    using error_type = ParseError;
    using allocator_type = NativeParserTraits::allocator_type;
};

struct PointerResultTraits : NativeParserTraits
{
    using expression_type = Expr *;
};

static_assert(ParserBackendTraits<NativeParserTraits>);
static_assert(!ParserBackendTraits<MissingBindingsTraits>);
static_assert(!ParserBackendTraits<PointerResultTraits>);

TYPED_TEST(ParserTest, ConstructionAllocatorAndAstLifetime)
{
    using Traits = typename TypeParam::traits_type;
    using Allocator = typename Traits::allocator_type;

    struct Resource : std::pmr::memory_resource
    {
        size_t allocations = 0, deallocations = 0;

        void * do_allocate(size_t size, size_t alignment) override
        {
            ++allocations;
            return std::pmr::new_delete_resource()->allocate(size, alignment);
        }

        void do_deallocate(void * p, size_t size, size_t alignment) override
        {
            ++deallocations;
            std::pmr::new_delete_resource()->deallocate(p, size, alignment);
        }

        bool do_is_equal(const std::pmr::memory_resource & other) const noexcept override
        {
            return this == &other;
        }
    };

    std::optional<typename Traits::expression_type> retained;
    auto basePath = this->state.rootPath(CanonPath::root);
    auto source = "\"" + std::string(20000, 'x') + "\\n\"";
    {
        Resource scratch;
        auto expression = this->template parse<false>(source, basePath, this->state.staticBaseEnv, Allocator{&scratch});
        ASSERT_TRUE(expression.has_value());
        retained = *expression;
        auto bindings = this->template parse<true>(
            "value = " + source + ";", basePath, this->state.staticBaseEnv, Allocator{&scratch});
        ASSERT_TRUE(bindings.has_value());
        auto failure =
            this->template parse<false>(source + " +", basePath, this->state.staticBaseEnv, Allocator{&scratch});
        ASSERT_FALSE(failure.has_value());
        EXPECT_TRUE(failure.error().isIncomplete());
        if constexpr (std::uses_allocator_v<TypeParam, Allocator>)
            EXPECT_GT(scratch.allocations, 0u);
        else
            EXPECT_EQ(scratch.allocations, 0u);
        EXPECT_EQ(scratch.allocations, scratch.deallocations);
    }
    Value value;
    this->state.eval(&retained->root(), value);
    this->state.forceValue(value, noPos);
    EXPECT_THAT(value, IsStringEq(std::string(20000, 'x') + '\n'));
}

TYPED_TEST(ParserTest, ExpectedResults)
{
    auto basePath = this->state.rootPath(CanonPath::root);
    auto expression = this->template parse<false>("42", basePath, this->state.staticBaseEnv);
    ASSERT_TRUE(expression.has_value());
    EXPECT_NE(dynamic_cast<ExprInt *>(&expression->root()), nullptr);
    EXPECT_EQ(&expression->owner(), &this->state.mem.exprs);
    auto bindings = this->template parse<true>("answer = 42;", basePath, this->state.staticBaseEnv);
    ASSERT_TRUE(bindings.has_value());
    EXPECT_EQ(bindings->root().attrs->size(), 1u);
    EXPECT_EQ(&bindings->owner(), &this->state.mem.exprs);
}

TYPED_TEST(ParserTest, ExpectedErrorsRetainDiagnostics)
{
    auto basePath = this->state.rootPath(CanonPath::root);
    auto expression = this->template parse<false>("(42", basePath, this->state.staticBaseEnv);
    ASSERT_FALSE(expression.has_value());
    EXPECT_TRUE(expression.error().isIncomplete());
    EXPECT_NE(std::string_view(expression.error().what()).find("(42"), std::string_view::npos);
    auto bindings = this->template parse<true>("answer =", basePath, this->state.staticBaseEnv);
    ASSERT_FALSE(bindings.has_value());
    EXPECT_TRUE(bindings.error().isIncomplete());
    EXPECT_NE(std::string_view(bindings.error().what()).find("answer ="), std::string_view::npos);
    for (auto source : {"{ a = ; }", "{ a = 1; a = 2; }"}) {
        auto invalid = this->template parse<false>(source, basePath, this->state.staticBaseEnv);
        ASSERT_FALSE(invalid.has_value());
        EXPECT_FALSE(invalid.error().isIncomplete());
    }
    auto duplicate = this->template parse<true>("a = 1; a = 2;", basePath, this->state.staticBaseEnv);
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_FALSE(duplicate.error().isIncomplete());
    EXPECT_THAT(this->eval("42"), IsIntEq(42));
}

TYPED_TEST(ParserTest, QuotedAttributeNames)
{
    ASSERT_THAT(
        this->eval(R"(
      let attrs = {
        "" = 1;
        "with space" = 2;
        "line\nbreak" = 3;
        "quo\"te" = 4;
        "\${literal}" = 5;
        "prefix-${"suffix"}" = 6;
      };
      copied = { inherit (attrs) "" "with space" "line\nbreak" "quo\"te" "\${literal}"; };
      in copied."" + copied."with space" + copied."line\nbreak"
        + copied."quo\"te" + copied."\${literal}" + attrs."prefix-suffix"
    )"),
        IsIntEq(21));
    ASSERT_THROW(this->eval(R"({ "a" = 1; "\a" = 2; })"), ParseError);
}

TYPED_TEST(ParserTest, FunctionsAndBindings)
{
    ASSERT_THAT(this->eval("let f = x: x + 1; in f 41"), IsIntEq(42));
    ASSERT_THAT(this->eval("(x :x) 42"), IsIntEq(42));
    ASSERT_THAT(this->eval("(_x:_x) 42"), IsIntEq(42));
    ASSERT_THAT(this->eval("({ x }: x) { x = 42; }"), IsIntEq(42));
    ASSERT_THAT(this->eval("({ x ? 41, ... }@args: x + args.y) { y = 1; }"), IsIntEq(42));
    ASSERT_THAT(this->eval("(args@{ x }: args.x) { x = 42; }"), IsIntEq(42));
    ASSERT_THAT(this->eval("let x = 40; in { inherit x; y.z = 2; }.x + 2"), IsIntEq(42));
    ASSERT_THAT(this->eval("{ inherit ({ x = 42; }) x; }.x"), IsIntEq(42));
    ASSERT_THAT(this->eval("{ x.a = 40; x = { b = 2; }; }.x.a + 2"), IsIntEq(42));
    ASSERT_THAT(this->eval("let { body = x + 1; x = 41; }"), IsIntEq(42));
    ASSERT_THAT(this->eval("{ inherit; inherit ({ x = 1; }); }"), IsAttrsOfSize(0));
}

TYPED_TEST(ParserTest, ApplicationOrderAndPositions)
{
    // Fold arguments in source order, including a nested application. Calls
    // begin at the opening parenthesis, not the forwarded lambda position.
    auto source = "\n  (x: y: x - y) (20 + 30) ((z: z) 8)";
    EXPECT_THAT(this->eval(source), IsIntEq(42));
    auto expression = this->parseExprFromString(source, this->state.rootPath(CanonPath::root));
    auto call = dynamic_cast<ExprCall *>(expression);
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->args->size(), 2u);
    auto position = this->state.positions[call->getPos()];
    EXPECT_EQ(position.line, 2u);
    EXPECT_EQ(position.column, 3u);

    // Without arguments the parentheses still forward the child's position.
    auto variable = this->parseExprFromString("\n  (__curPos)", this->state.rootPath(CanonPath::root));
    position = this->state.positions[variable->getPos()];
    EXPECT_EQ(position.line, 2u);
    EXPECT_EQ(position.column, 4u);
}

TYPED_TEST(ParserTest, OperatorsAndControlFlow)
{
    ASSERT_THAT(this->eval("if 2 < 3 && !(3 == 4) then 6 * 7 else 0"), IsIntEq(42));
    ASSERT_THAT(this->eval("with { x = 42; }; assert x >= 42; x"), IsIntEq(42));
    ASSERT_THAT(this->eval("{ x = 0; } // { x = 42; }.missing or {}"), IsAttrsOfSize(1));
    ASSERT_THAT(this->eval("({ x = 0; } // { x = 42; }).x"), IsIntEq(42));
    ASSERT_THAT(this->eval("builtins.length ([ 1 ] ++ [ 2 3 ])"), IsIntEq(3));
    ASSERT_THAT(this->eval("{ x.y = 1; } ? x.y"), IsTrue());
    ASSERT_THAT(this->eval("5 |> (x: x + 1) |> (x: x * 7)"), IsIntEq(42));
    ASSERT_THAT(this->eval("(x: x + 1) <| (x: x * 2) <| 20"), IsIntEq(41));
}

TYPED_TEST(ParserTest, PrefixOperandsAndChainedAttributeTests)
{
    // These expressions need not type-check when forced. Nix's lazy language
    // still allows them in an unused binding, so a backend must accept it.
    for (auto source : {"1 + !2", "1 * !!2", "1 ++ !-2", "-!1 + 2", "1 / -!2", "{} ? a ? b"}) {
        SCOPED_TRACE(source);
        EXPECT_THAT(this->eval(std::string("let unused = ") + source + "; in 42"), IsIntEq(42));
    }
    auto expression = this->parseExprFromString("{} ? a ? b", this->state.rootPath(CanonPath::root));
    auto outer = dynamic_cast<ExprOpHasAttr *>(expression);
    ASSERT_NE(outer, nullptr);
    EXPECT_NE(dynamic_cast<ExprOpHasAttr *>(outer->e), nullptr);
    for (auto source : {"!=1", "1 + !=2"}) {
        SCOPED_TRACE(source);
        EXPECT_THROW(this->parseExprFromString(source, this->state.rootPath(CanonPath::root)), ParseError);
    }
}

TYPED_TEST(ParserTest, PipeGroups)
{
    // Opposite directions need parentheses; arithmetic binds inside each
    // operand. Noncommuting functions make a wrong associativity observable.
    EXPECT_THAT(this->eval("(1 + 4 |> (x: x + 1)) |> (x: x * 7)"), IsIntEq(42));
    EXPECT_THAT(this->eval("(x: x + 2) <| (x: x * 2) <| 10 + 10"), IsIntEq(42));
    EXPECT_THAT(this->eval("(x: x + 1) <| (20 |> (x: x * 2))"), IsIntEq(41));
    EXPECT_THAT(this->eval("((x: x + 1) <| 20) |> (x: x * 2)"), IsIntEq(42));
    for (auto source : {"1 |> (x: x) <| 2", "(x: x) <| 1 |> (x: x)"}) {
        SCOPED_TRACE(source);
        EXPECT_THROW(this->parseExprFromString(source, this->state.rootPath(CanonPath::root)), ParseError);
    }
    std::string into = "let f = x: x; in 42";
    std::string from = "let f = x: x; in ";
    for (size_t i = 0; i < 512; ++i) {
        into += " |> f";
        from += "f <| ";
    }
    EXPECT_THAT(this->eval(into), IsIntEq(42));
    EXPECT_THAT(this->eval(from + "42"), IsIntEq(42));
}

TYPED_TEST(ParserTest, LegacyOrArguments)
{
    EXPECT_THAT(this->eval("with { or = 42; }; (x: x) or"), IsIntEq(42));
    EXPECT_THAT(this->eval("let or = x: x; in map or [ 42 ] == [ 42 ]"), IsTrue());
    EXPECT_THAT(this->eval("let f = x: x; or = 42; in f (f or)"), IsIntEq(42));
    EXPECT_THAT(this->eval("let or = 42; in builtins.head [ (x: x) or ]"), IsIntEq(42));
    EXPECT_THAT(this->eval("let or = 41; in (x: x + 1) (x: x) or"), IsIntEq(42));
    EXPECT_THAT(this->eval("let or = 42; in {}.missing or (x: x) or"), IsIntEq(42));
    EXPECT_THAT(this->eval("{ or = 42; }.or"), IsIntEq(42));
    EXPECT_THAT(this->eval("{}.missing or 42"), IsIntEq(42));
}

TYPED_TEST(ParserTest, PipesRequireExperimentalFeature)
{
    auto previous = experimentalFeatureSettings.experimentalFeatures.get();
    Finally restore([&] { experimentalFeatureSettings.experimentalFeatures = previous; });
    // Options must reach every recursive entry, not just the outer expression.
    for (auto pipe : {
             "5 |> (x: x + 1) |> (x: x * 7)",
             "(x: x + 2) <| (x: x * 2) <| 20",
             "let x = 42 |> (x: x); in x",
             "({ x ? 42 |> (x: x) }: x) {}",
             "[ (42 |> (x: x)) ]",
             R"("${"value" |> (x: x)}")",
             R"(''${"value" |> (x: x)}'')",
             R"(./${"value" |> (x: x)})",
             R"({ ${"name" |> (x: x)} = 42; })",
         }) {
        SCOPED_TRACE(pipe);
        experimentalFeatureSettings.experimentalFeatures = {};
        EXPECT_THROW(this->parseExprFromString(pipe, this->state.rootPath(CanonPath::root)), ParseError);
        EXPECT_THROW(
            this->parseReplBindings(
                std::string("value = ") + pipe + ";", this->state.rootPath(CanonPath::root), this->state.staticBaseEnv),
            ParseError);
        experimentalFeatureSettings.experimentalFeatures = {Xp::PipeOperators};
        EXPECT_NO_THROW(this->parseExprFromString(pipe, this->state.rootPath(CanonPath::root)));
        EXPECT_NO_THROW(this->parseReplBindings(
            std::string("value = ") + pipe + ";", this->state.rootPath(CanonPath::root), this->state.staticBaseEnv));
    }
    experimentalFeatureSettings.experimentalFeatures = {};
    EXPECT_THAT(this->eval("with { or = 42; }; (x: x) or"), IsIntEq(42));
    EXPECT_THAT(this->eval("builtins.length [ ({} ? a ? b) ]"), IsIntEq(1));
}

TYPED_TEST(ParserTest, BinaryOperatorPositions)
{
    for (std::string op : {"+", "-", "*", "/", "==", "!=", "<", ">", "&&", "||", "->", "//", "++"}) {
        SCOPED_TRACE(op);
        auto expression = this->parseExprFromString("  1 " + op + " 2", this->state.rootPath(CanonPath::root));
        auto position = this->state.positions[expression->getPos()];
        EXPECT_EQ(position.line, 1u);
        EXPECT_EQ(position.column, 5u);
    }
}

TYPED_TEST(ParserTest, StringsAndNativeLifetime)
{
    ASSERT_THAT(this->eval(R"("")"), IsStringEq(""));
    ASSERT_THAT(this->eval(R"("ordinary literal")"), IsStringEq("ordinary literal"));
    ASSERT_THAT(this->eval(R"("${"only interpolation"}")"), IsStringEq("only interpolation"));
    ASSERT_THAT(this->eval(R"("${"a"}${"b"}")"), IsStringEq("ab"));
    ASSERT_THAT(this->eval(R"("hello ${"world"}\n")"), IsStringEq("hello world\n"));
    ASSERT_THAT(this->eval("\"a\r\nb\rc\""), IsStringEq("a\nb\nc"));
    ASSERT_THAT(this->eval("\"\xff\""), IsStringEq("\xff"));
    ASSERT_THAT(this->eval("\"\\\xff\""), IsStringEq("\xff"));
    ASSERT_THAT(this->eval("''\xff''"), IsStringEq("\xff"));
    ASSERT_THAT(this->eval("'' ''\\\xff''"), IsStringEq("\xff"));
    ASSERT_THAT(this->eval("''\n  first\n    second\n  ''"), IsStringEq("first\n  second\n"));
    ASSERT_THAT(this->eval("''\n  ${\"first\"}\n    second\n  ''"), IsStringEq("first\n  second\n"));
    ASSERT_THAT(this->eval("''\n  a''\\n  b\n  ''"), IsStringEq("a\nb\n"));
    ASSERT_THAT(this->eval("{ ${\"x\"} = 42; }.\"x\""), IsIntEq(42));
    // Force a closure after parsing has released its temporary storage.
    auto value = this->eval("let captured = \"a sufficiently long string to require allocation\"; in x: captured + x");
    auto argument = this->eval("\"!\"");
    Value result;
    this->state.callFunction(value, argument, result, noPos);
    this->state.forceValue(result, noPos);
    ASSERT_THAT(result, IsStringEq("a sufficiently long string to require allocation!"));
}

TYPED_TEST(ParserTest, StringRunBoundaries)
{
    ASSERT_THAT(this->eval(R"("prefix$${name}suffix")"), IsStringEq("prefix$${name}suffix"));
    ASSERT_THAT(this->eval(R"("prefix$$${"x"}suffix")"), IsStringEq("prefix$$xsuffix"));
    ASSERT_THAT(this->eval("''prefix$${name}suffix''"), IsStringEq("prefix$${name}suffix"));
    ASSERT_THAT(this->eval("''prefix'''suffix''"), IsStringEq("prefix''suffix"));
    ASSERT_THAT(this->eval("''prefix''${name}suffix''"), IsStringEq("prefix${name}suffix"));
    auto literal = std::string(4096, 'x') + '\xff';
    ASSERT_THAT(
        this->eval("\"" + literal + "\\n${\"!\"}\\t" + literal + "\""), IsStringEq(literal + "\n!\t" + literal));
    ASSERT_THAT(this->eval("\"" + literal + "\r\n" + literal + "\r\""), IsStringEq(literal + "\n" + literal + "\n"));
    ASSERT_THAT(this->eval("''\n  " + literal + "\n  ''"), IsStringEq(literal + "\n"));
}

TYPED_TEST(ParserTest, BytePreservingInputReaders)
{
    for (std::string_view bytes :
         {"caf\xc3\xa9",
          "\xf0\x9f\x98\x80",
          "\xf4\x8f\xbf\xbf",
          "\xff",
          "\xc0\x80",
          "\xed\xa0\x80",
          "\xe2\x82",
          "\xf4\x90\x80\x80"}) {
        for (size_t padding : {0, 1024}) {
            auto literal = std::string(bytes);
            // Cover short and padded inputs, including invalid UTF-8.
            // Interpolation must preserve the original literal bytes.
            auto source = std::string(padding, ' ') + "\"" + literal + "${\"!\"}\"";
            ASSERT_THAT(this->eval(source), IsStringEq(literal + "!"));
        }
    }
}

TYPED_TEST(ParserTest, IdentifierLifetime)
{
    // Temporary lexeme views must be interned before the parse arena/input dies.
    auto closure = this->eval(
        "let a_long_captured_variable_name = 40; in "
        "a_long_argument_set_name@{ a_long_formal_argument_name }: "
        "a_long_captured_variable_name + a_long_argument_set_name.a_long_formal_argument_name");
    auto argument = this->eval("{ a_long_formal_argument_name = 2; }");
    Value result;
    this->state.callFunction(closure, argument, result, noPos);
    this->state.forceValue(result, noPos);
    ASSERT_THAT(result, IsIntEq(42));
}

TYPED_TEST(ParserTest, EscapedStringRuns)
{
    std::string source = "\"", expected;
    for (int i = 0; i < 512; ++i) {
        source += R"(a\nb\tc\\d\"e)";
        expected += "a\nb\tc\\d\"e";
        if (i % 31 == 0) {
            source += R"(${"!"})";
            expected += '!';
        }
    }
    source += '"';
    ASSERT_THAT(this->eval(source), IsStringEq(expected));
    ASSERT_THAT(this->eval("''\n  a''\\n  b\n    c''\\t''\\nd\n  ''"), IsStringEq("a\nb\n  c\t\nd\n"));
    ASSERT_THAT(this->eval(R"(''''\n''\ '')"), IsStringEq("\n "));
    ASSERT_THAT(this->eval(R"(''''\n''\t''\ '')"), IsStringEq("\n\t "));
}

TYPED_TEST(ParserTest, LiteralsAndSourcePositions)
{
    ASSERT_THAT(this->eval(".5"), IsFloatEq(0.5));
    ASSERT_THAT(this->eval("1.5e2"), IsFloatEq(150));
    ASSERT_THAT(this->eval("9223372036854775807"), IsIntEq(INT64_MAX));
    ASSERT_THAT(this->eval("./a/${\"b\"}"), IsPathEq("/a/b"));
    ASSERT_THAT(this->eval("./a/b/${\"c\"}/d${\"e\"}"), IsPathEq("/a/b/c/de"));
    ASSERT_THAT(this->eval("1/2"), IsPathEq("/1/2"));
    ASSERT_THAT(this->eval(".5/x"), IsPathEq("/.5/x"));
    ASSERT_THAT(this->eval("builtins.toString if/x"), IsStringEq("/if/x"));
    ASSERT_THAT(this->eval("\r1"), IsIntEq(1));
    ASSERT_THAT(this->eval("# comment\r42"), IsIntEq(42));
    auto expression = this->parseExprFromString("\n  __curPos", this->state.rootPath(CanonPath::root));
    auto position = this->state.positions[expression->getPos()];
    EXPECT_EQ(position.line, 2u);
    EXPECT_EQ(position.column, 3u);
}

TYPED_TEST(ParserTest, FloatTokenBoundaries)
{
    // An exponent belongs to a float only when it includes digits. Otherwise,
    // the following identifier starts another list element, as in the Flex lexer.
    auto prefix = std::string("let e = 7; e- = 8; E = 9; in ");
    auto elements = std::string("[0.0e 1.e- .5E]");
    ASSERT_THAT(this->eval(prefix + "builtins.length " + elements), IsIntEq(6));
    ASSERT_THAT(this->eval(prefix + "builtins.elemAt " + elements + " 1"), IsIntEq(7));
    ASSERT_THAT(this->eval(prefix + "builtins.elemAt " + elements + " 3"), IsIntEq(8));
    ASSERT_THAT(this->eval(prefix + "builtins.elemAt " + elements + " 5"), IsIntEq(9));
    ASSERT_THAT(this->eval("1."), IsFloatEq(1));
    ASSERT_THAT(this->eval("1.25e-2"), IsFloatEq(0.0125));
    ASSERT_THAT(this->eval("0.5E+2"), IsFloatEq(50));
    // Leading zeroes belong to the integer token; the dot starts another float.
    ASSERT_THAT(this->eval("builtins.length [00.5]"), IsIntEq(2));
    ASSERT_THAT(this->eval("builtins.elemAt [00.5] 1"), IsFloatEq(0.5));
}

TYPED_TEST(ParserTest, PathLiteralBoundaries)
{
    ASSERT_THAT(this->eval("./a/b/${\"c\"}/d/e${\"f\"}/g"), IsPathEq("/a/b/c/d/ef/g"));
    ASSERT_THAT(this->eval("./a/${\"b\"}${\"c\"}/d"), IsPathEq("/a/bc/d"));
    ASSERT_THAT(this->eval("builtins.toString /a/b/c"), IsStringEq("/a/b/c"));
    ASSERT_THAT(this->eval("./a/${\"b\"}//c"), IsPathEq("/a/b/c"));
    ASSERT_THAT(this->eval("./a/${\"b\"}///${\"c\"}/d"), IsPathEq("/a/b/c/d"));
    ASSERT_THAT(this->eval("./a//${\"b\"}/c"), IsPathEq("/a/b/c"));
    ASSERT_THAT(this->eval("./a//b/${\"c\"}"), IsPathEq("/a/b/c"));
    for (auto source : {"./a/", "./a//b", "./a///${\"b\"}", "./a/${\"b\"}/", "./a/${\"b\"}//"})
        EXPECT_THROW(this->parseExprFromString(source, this->state.rootPath(CanonPath::root)), ParseError);
}

TYPED_TEST(ParserTest, LookaheadBoundaries)
{
    for (std::string keyword : {"if", "then", "else", "assert", "with", "let", "in", "rec", "inherit", "or"}) {
        EXPECT_THROW(
            this->parseExprFromString("(" + keyword + ": 42)", this->state.rootPath(CanonPath::root)), ParseError);
        for (std::string suffix : {"Suffix", "_suffix", "-suffix", "'suffix"}) {
            auto name = keyword + suffix;
            ASSERT_THAT(this->eval("{ " + name + " = 42; }." + name), IsIntEq(42));
        }
        ASSERT_THAT(this->eval("builtins.toString " + keyword + "/path"), IsStringEq("/" + keyword + "/path"));
    }
    ASSERT_THAT(this->eval("http://example.invalid/a-b?x=1"), IsStringEq("http://example.invalid/a-b?x=1"));
    ASSERT_THAT(this->eval("builtins.head [ https://example.invalid/a/b ]"), IsStringEq("https://example.invalid/a/b"));
    ASSERT_THAT(this->eval("builtins.toString or:uri"), IsStringEq("or:uri"));
    ASSERT_THAT(this->eval("(x: builtins.toString x) or/path"), IsStringEq("/or/path"));
    ASSERT_THAT(this->eval("{}.missing or/* comment */42"), IsIntEq(42));
    ASSERT_THAT(this->eval("(x: x) .5"), IsFloatEq(0.5));
    ASSERT_THAT(this->eval("(x: y: x + y) /* first */ 40 /** second */ 2"), IsIntEq(42));
}

TYPED_TEST(ParserTest, DocumentationComments)
{
    auto parse = [&](const std::string & source) {
        return this->parseExprFromString(source, this->state.rootPath(CanonPath::root));
    };
    auto root = dynamic_cast<ExprLambda *>(parse("/** root */ x: x"));
    ASSERT_NE(root, nullptr);
    ASSERT_TRUE(root->docComment);
    EXPECT_NE(root->docComment.getInnerText(this->state.positions).find("root"), std::string::npos);

    auto attrs = dynamic_cast<ExprAttrs *>(parse("{ /** outer */ f = x: /** inner */ y: x; }"));
    auto outer = dynamic_cast<ExprLambda *>(attrs->attrs->begin()->second.e);
    auto inner = dynamic_cast<ExprLambda *>(outer->body);
    ASSERT_TRUE(outer->docComment);
    ASSERT_TRUE(inner->docComment);
    EXPECT_NE(outer->docComment.getInnerText(this->state.positions).find("outer"), std::string::npos);
    EXPECT_NE(inner->docComment.getInnerText(this->state.positions).find("inner"), std::string::npos);

    auto last = dynamic_cast<ExprLambda *>(parse("/** first */ /** last */ # ordinary\n /* ordinary */ x: x"));
    ASSERT_TRUE(last->docComment);
    EXPECT_NE(last->docComment.getInnerText(this->state.positions).find("last"), std::string::npos);
    auto absent = dynamic_cast<ExprLambda *>(parse("/**** ordinary */ x: x"));
    EXPECT_FALSE(absent->docComment);
}

TYPED_TEST(ParserTest, DocumentationCommentChains)
{
    std::string source = "{ ";
    for (int i = 0; i < 128; ++i)
        source += "/** earlier */ # line comment\r\n /* ordinary */ ";
    source += "/** attached */ f = x: x; /** next token */ g = y: y; } /** at EOF */";
    auto attrs = dynamic_cast<ExprAttrs *>(this->parseExprFromString(source, this->state.rootPath(CanonPath::root)));
    ASSERT_NE(attrs, nullptr);
    for (auto [name, doc] : {std::pair{"f", "attached"}, std::pair{"g", "next token"}}) {
        auto lambda = dynamic_cast<ExprLambda *>(attrs->attrs->at(this->state.symbols.create(name)).e);
        ASSERT_NE(lambda, nullptr);
        ASSERT_TRUE(lambda->docComment);
        EXPECT_EQ(lambda->docComment.getInnerText(this->state.positions), std::string(doc) + " \n");
    }
}

TYPED_TEST(ParserTest, DocumentationAcrossFormalLookahead)
{
    auto root = dynamic_cast<ExprLambda *>(this->parseExprFromString(
        "/** first */ /** root */ { arg ? (/** default */ x: x), ... }: /** body */ y: y",
        this->state.rootPath(CanonPath::root)));
    ASSERT_NE(root, nullptr);
    ASSERT_TRUE(root->docComment);
    EXPECT_EQ(root->docComment.getInnerText(this->state.positions), "root \n");
    auto body = dynamic_cast<ExprLambda *>(root->body);
    ASSERT_NE(body, nullptr);
    ASSERT_TRUE(body->docComment);
    EXPECT_EQ(body->docComment.getInnerText(this->state.positions), "body \n");
    auto defaultExpr = dynamic_cast<ExprLambda *>(root->getFormals()->formals[0].def);
    ASSERT_NE(defaultExpr, nullptr);
    ASSERT_TRUE(defaultExpr->docComment);
    EXPECT_EQ(defaultExpr->docComment.getInnerText(this->state.positions), "default \n");
}

TYPED_TEST(ParserTest, DocumentationOnlyFromComments)
{
    for (auto prefix : {"# /** ignored */\n", "/* ordinary /** ignored */", "/**/ /***/ /**** ignored */"}) {
        auto lambda = dynamic_cast<ExprLambda *>(
            this->parseExprFromString(std::string(prefix) + " x: x", this->state.rootPath(CanonPath::root)));
        ASSERT_NE(lambda, nullptr);
        EXPECT_FALSE(lambda->docComment);
    }
    auto attrs = dynamic_cast<ExprAttrs *>(this->parseExprFromString(
        R"({ text = "${"x"}/** literal */"; indented = ''${"x"}/** literal */''; f = x: x; })",
        this->state.rootPath(CanonPath::root)));
    ASSERT_NE(attrs, nullptr);
    auto lambda = dynamic_cast<ExprLambda *>(attrs->attrs->at(this->state.symbols.create("f")).e);
    ASSERT_NE(lambda, nullptr);
    EXPECT_FALSE(lambda->docComment);
}

TYPED_TEST(ParserTest, TriviaBetweenTokens)
{
    for (const std::vector<std::string_view> & tokens : {
             std::vector<std::string_view>{"let", "f", "=", "x", ":", "x", ";", "in", "f", "42"},
             {"(", "{", "x", "?", "41", ",", "...", "}", ":", "x", "+", "1", ")", "{", "}"},
             {"{", "a", ".", "b", "=", "42", ";", "}", ".", "a", ".", "b", "or", "0"},
             {"let", "a",       "=", "{", "x", "=", "42", ";", "}", ";", "in",
              "{",   "inherit", "(", "a", ")", "x", ";",  "}", ".", "x"},
             {"{", "${", "\"x\"", "}", "=", "42", ";", "}", ".", "x"},
             {"assert",
              "!",
              "false",
              ";",
              "with",
              "{",
              "x",
              "=",
              "42",
              ";",
              "}",
              ";",
              "if",
              "true",
              "then",
              "x",
              "else",
              "0"},
         }) {
        for (auto trivia : {"/** boundary */", " /** boundary */ # line\r\n /* ordinary */ "}) {
            std::string source = "/** leading */";
            for (auto token : tokens) {
                source += token;
                source += trivia;
            }
            SCOPED_TRACE(source);
            EXPECT_THAT(this->eval(source), IsIntEq(42));
        }
    }
}

TYPED_TEST(ParserTest, RejectsInvalidBindingsAndLiterals)
{
    for (auto source : {
             "{ x = 1; x = 2; }",
             "{ x, x }: x",
             "x@{ x }: x",
             "let ${builtins.toString 1} = 1; in 1",
             "{ inherit ${builtins.toString 1}; }",
             "9223372036854775808",
             "1.0e9999",
             "{ x = ; }",
             "x@y: x",
             "1 == 2 == 3",
             "1 |> (x: x) <| 2",
         }) {
        SCOPED_TRACE(source);
        EXPECT_THROW(this->parseExprFromString(source, this->state.rootPath(CanonPath::root)), ParseError);
    }
}

TYPED_TEST(ParserTest, NativeConstructionAfterErrors)
{
    // Fail after constructing children, including a documented lambda and a
    // decoded literal. A later parse must have its own documentation state,
    // and already-returned closures must remain valid across failures.
    auto closure = this->eval(R"(let captured = "retained\n"; in x: captured + x)");
    for (auto source : {
             R"({ f = /** discarded */ x: "decoded\n"; bad = ; })",
             R"({ f = /** discarded */ x: x; duplicate = 1; duplicate = 2; })",
             R"({ f = x: x; huge = 9223372036854775808; })",
         }) {
        SCOPED_TRACE(source);
        EXPECT_THROW(this->parseExprFromString(source, this->state.rootPath(CanonPath::root)), ParseError);
        auto lambda =
            dynamic_cast<ExprLambda *>(this->parseExprFromString("x: x", this->state.rootPath(CanonPath::root)));
        ASSERT_NE(lambda, nullptr);
        EXPECT_FALSE(lambda->docComment);
    }
    auto argument = this->eval(R"("value")");
    Value result;
    this->state.callFunction(closure, argument, result, noPos);
    this->state.forceValue(result, noPos);
    ASSERT_THAT(result, IsStringEq("retained\nvalue"));
}

TYPED_TEST(ParserTest, DeeplyNestedExpressions)
{
    for (auto depth : {512, 8192}) {
        SCOPED_TRACE(depth);
        auto source = std::string(depth, '(') + "42" + std::string(depth, ')');
        EXPECT_THAT(this->eval(source), IsIntEq(42));
        EXPECT_NO_THROW(this->parseReplBindings(
            "deep = " + source + ";", this->state.rootPath(CanonPath::root), this->state.staticBaseEnv));
        // A missing delimiter is still incomplete input, even at this depth.
        source.pop_back();
        try {
            this->parseExprFromString(source, this->state.rootPath(CanonPath::root));
            FAIL() << "expected an unmatched parenthesis";
        } catch (ParseError & e) {
            EXPECT_TRUE(e.isIncomplete());
        }
    }
    EXPECT_THAT(this->eval("42"), IsIntEq(42));
}

TYPED_TEST(ParserTest, LongFlatOperatorChain)
{
    for (auto suffix : {" + 1", " + 2 * 3", " && !false"}) {
        std::string source = "0";
        for (int i = 0; i < 12000; ++i)
            source += suffix;
        EXPECT_NO_THROW(this->parseExprFromString(source, this->state.rootPath(CanonPath::root)));
    }
}

TYPED_TEST(ParserTest, ReplBindingsAndIncompleteInput)
{
    auto bindings =
        this->parseReplBindings("x = 42; y.z = 1;", this->state.rootPath(CanonPath::root), this->state.staticBaseEnv);
    ASSERT_EQ(bindings->attrs->size(), 2u);
    for (auto source : {"let x = 1; in", "\"unfinished", "\"unfinished\\", "\"${1", "\"$", "\"$$"}) {
        SCOPED_TRACE(source);
        try {
            this->parseExprFromString(source, this->state.rootPath(CanonPath::root));
            FAIL() << "expected incomplete input";
        } catch (ParseError & e) {
            EXPECT_TRUE(e.isIncomplete());
        }
    }
}

} // namespace nix
