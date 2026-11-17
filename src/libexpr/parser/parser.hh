#include "nix/expr/eval.hh"
#include "nix/expr/nixexpr.hh"
#include "nix/util/pos-idx.hh"

#include <boost/unordered/unordered_flat_map_fwd.hpp>

namespace nix {

using DocCommentMap = boost::unordered_flat_map<PosIdx, DocComment, std::hash<PosIdx>>;

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
