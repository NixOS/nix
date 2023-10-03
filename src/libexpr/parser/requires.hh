
#ifndef BISON_HEADER
#define BISON_HEADER

#include <variant>

#include "eval-settings.hh"
#include "eval.hh"
#include "globals.hh"
#include "nixexpr.hh"
#include "util.hh"

namespace nix {

struct ParseData
{
    EvalState & state;
    SymbolTable & symbols;
    Expr * result;
    SourcePath basePath;
    PosTable::Origin origin;
    std::optional<ErrorInfo> error;
};

struct ParserFormals
{
    std::vector<Formal> formals;
    bool ellipsis = false;
};

} // namespace nix

// using C a struct allows us to avoid having to define the special
// members that using string_view here would implicitly delete.
struct StringToken
{
    const char * p;
    size_t l;
    bool hasIndentation;
    operator std::string_view() const
    {
        return {p, l};
    }
};

#define YY_DECL int yylex(YYSTYPE * yylval_param, YYLTYPE * yylloc_param, yyscan_t yyscanner, nix::ParseData * data)

#endif // BISON_HEADER
