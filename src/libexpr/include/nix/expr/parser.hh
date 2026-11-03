#pragma once
///@file Low-level parsing without variable binding or evaluation.

#include "nix/expr/eval.hh"
#include <concepts>
#include <expected>
#include <memory>
#include <memory_resource>
#include <span>

namespace nix {

struct ParserState;

/**
 * A non-owning, non-null root and its expression arena. The arena, symbol table
 * and position table must outlive use of the tree. This handle does not extend
 * their lifetimes; its constructor requires a node belonging to the given arena.
 */
template<std::derived_from<Expr> Node>
class ParsedAst
{
    std::reference_wrapper<Node> root_;
    std::reference_wrapper<Exprs> owner_;

public:
    ParsedAst(Node & root, Exprs & owner)
        : root_(root)
        , owner_(owner)
    {
    }

    Node & root() const
    {
        return root_.get();
    }

    Exprs & owner() const
    {
        return owner_.get();
    }
};

/**
 * Associated AST, diagnostic and construction-allocator types for a backend.
 * The requires parameters check that the three result/diagnostic aliases exist;
 * they do not construct any objects or run the accessors below. Checking const
 * handles lets callers inspect a result without changing the handle itself.
 */
template<typename Traits>
concept ParserBackendTraits = requires(
    const typename Traits::expression_type & expression,
    const typename Traits::repl_bindings_type & bindings,
    const typename Traits::error_type & error) {
    // Name the allocator callers may supply for temporary parsing storage.
    // value_type checks its basic shape, not the full allocator requirements.
    typename Traits::allocator_type;
    typename Traits::allocator_type::value_type;

    // Expression parsing returns an arbitrary Expr; REPL bindings need the
    // attributes of an ExprAttrs. Exact mutable references let callers bind
    // variables in the returned tree even when the handle itself is const.
    { expression.root() } -> std::same_as<Expr &>;
    { bindings.root() } -> std::same_as<ExprAttrs &>;

    // Expose the arena whose lifetime bounds the borrowed tree. These type
    // checks cannot prove that the roots actually belong to the reported arena.
    { expression.owner() } -> std::same_as<Exprs &>;
    { bindings.owner() } -> std::same_as<Exprs &>;

    // The REPL uses this distinction to request more input after an incomplete
    // parse. Ordinary syntax errors must be reported without requesting more.
    { error.isIncomplete() } -> std::same_as<bool>;

    // Keep diagnostics compatible with Nix's existing error reporting and
    // exception handlers when the buffer entry points throw a returned error.
    requires std::derived_from<typename Traits::error_type, Error>;

    // Callers may preserve the result while copying its diagnostic elsewhere
    // or throwing it. This is stronger than std::expected's error requirements.
    requires std::copy_constructible<typename Traits::error_type>;

    // Parsing without an explicit allocator uses a default-constructed one.
    requires std::default_initializable<typename Traits::allocator_type>;
};

/** Both parse modes use borrowed native AST handles and Nix diagnostics. */
struct NativeParserTraits
{
    using expression_type = ParsedAst<Expr>;
    using repl_bindings_type = ParsedAst<ExprAttrs>;
    using error_type = ParseError;
    // Byte allocation allows scratch storage for different parser value types;
    // a caller can select its memory_resource independently of the AST arena.
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;
};

/**
 * A backend constructs expressions or REPL bindings from a mutable buffer and
 * shared parser state. The caller supplies two trailing NUL bytes in the span
 * and keeps the input alive until parsing returns. Implementations may modify
 * the buffer and must consume the complete expression or binding input.
 *
 * Returned nodes belong to state.exprs; borrowed input and temporary storage
 * must not escape into the tree. Positions refer to state.origin. Parsing does
 * not bind variables or evaluate expressions. Parse failures return error_type,
 * retaining positions and the incomplete-input marker for REPL continuation.
 * Other failures, such as allocation or source-access errors, may throw.
 *
 * traits_type names the result, error and construction-allocator types. Backends
 * may opt into std::uses_allocator construction; the supplied allocator is for
 * temporary parsing storage only. Native nodes still belong to state.exprs.
 * Allocator-unaware backends are default-constructed. The concept checks types
 * and construction; ownership and error-handling remain semantic requirements.
 */
template<typename Backend>
concept ParserBackend =
    // Validate the associated types before checking construction and parsing.
    ParserBackendTraits<typename Backend::traits_type>

    // Match std::make_obj_using_allocator<Backend>(allocator), with no other
    // constructor arguments. uses_allocator normally opts in via a compatible
    // Backend::allocator_type; an allocator-taking constructor alone does not.
    && ((std::uses_allocator_v<Backend, typename Backend::traits_type::allocator_type>
         // Accept the standard form Backend(std::allocator_arg, allocator),
         // where the tag distinguishes the allocator from ordinary arguments.
         && (std::constructible_from<
                 Backend,
                 std::allocator_arg_t,
                 const typename Backend::traits_type::allocator_type &>
             // Or accept an allocator as the last argument: Backend(allocator).
             || std::constructible_from<Backend, const typename Backend::traits_type::allocator_type &>) )
        // Only backends that do not opt in may ignore the allocator and use
        // Backend(). This lets Bison participate without an allocator adapter.
        || (!std::uses_allocator_v<Backend, typename Backend::traits_type::allocator_type>
            && std::default_initializable<Backend>) )

    // Call through a mutable backend, as the driver does. Static functions and
    // instance methods both qualify; input and state are mutable during parsing.
    && requires(Backend & backend, std::span<char> input, ParserState & state) {
           // A complete expression produces its declared handle or diagnostic.
           // same_as requires an expected value, rejecting references to shared
           // results and types that merely convert to the expected result.
           {
               backend.parseExpr(input, state)
           } -> std::same_as<std::expected<
               typename Backend::traits_type::expression_type,
               typename Backend::traits_type::error_type>>;
           // REPL assignments have a separate entry point and handle type, but
           // use the same diagnostic type and return their result by value too.
           {
               backend.parseReplBindings(input, state)
           } -> std::same_as<std::expected<
               typename Backend::traits_type::repl_bindings_type,
               typename Backend::traits_type::error_type>>;
       };

struct BisonParserBackend
{
    using traits_type = NativeParserTraits;

    [[nodiscard]] static std::expected<ParsedAst<Expr>, ParseError>
    parseExpr(std::span<char> input, ParserState & state);
    [[nodiscard]] static std::expected<ParsedAst<ExprAttrs>, ParseError>
    parseReplBindings(std::span<char> input, ParserState & state);
};

/**
 * Construct native expressions without binding variables or evaluating them.
 * The Flex scanner requires two trailing NUL bytes, included in length,
 * and may overwrite the mutable buffer while parsing.
 * Expressions and symbols are owned by the supplied arenas. The origin must
 * retain the original source (or refer to its file) for diagnostic positions.
 */
Expr * parseExprFromBuf(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    Exprs & exprs,
    SymbolTable & symbols,
    const EvalSettings & settings,
    PosTable & positions,
    DocCommentMap & docComments,
    const ref<SourceAccessor> rootFS);

/**
 * Parse REPL bindings from a buffer.
 * Returns ExprAttrs with bindings to add to scope.
 */
ExprAttrs * parseReplBindingsFromBuf(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    Exprs & exprs,
    SymbolTable & symbols,
    const EvalSettings & settings,
    PosTable & positions,
    DocCommentMap & docComments,
    const ref<SourceAccessor> rootFS);

} // namespace nix
