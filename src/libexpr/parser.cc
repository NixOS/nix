#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "aterm.hh"
#include "parser.hh"
#include "nixexpr-ast.hh"


struct ParseData 
{
    Expr result;
    Path basePath;
    Path path;
    string error;
};


extern "C" {

#include "parser-tab.h"
#include "lexer-tab.h"
    
/* Callbacks for getting from C to C++.  Due to a (small) bug in the
   GLR code of Bison we cannot currently compile the parser as C++
   code. */

void setParseResult(ParseData * data, ATerm t)
{
    data->result = t;
}

ATerm absParsedPath(ParseData * data, ATerm t)
{
    return toATerm(absPath(aterm2String(t), data->basePath));
}
    
void parseError(ParseData * data, char * error, int line, int column)
{
    data->error = (format("%1%, at `%2%':%3%:%4%")
        % error % data->path % line % column).str();
}
        
ATerm fixAttrs(int recursive, ATermList as)
{
    ATermList bs = ATempty, cs = ATempty;
    ATermList * is = recursive ? &cs : &bs;
    for (ATermIterator i(as); i; ++i) {
        ATermList names;
        Expr src;
        ATerm pos;
        if (matchInherit(*i, src, names, pos)) {
            bool fromScope = matchScope(src);
            for (ATermIterator j(names); j; ++j) {
                Expr rhs = fromScope ? makeVar(*j) : makeSelect(src, *j);
                *is = ATinsert(*is, makeBind(*j, rhs, pos));
            }
        } else bs = ATinsert(bs, *i);
    }
    if (recursive)
        return makeRec(bs, cs);
    else
        return makeAttrs(bs);
}

const char * getPath(ParseData * data)
{
    return data->path.c_str();
}

int yyparse(yyscan_t scanner, ParseData * data);
}


static void checkAttrs(ATermMap & names, ATermList bnds)
{
    for (ATermIterator i(bnds); i; ++i) {
        ATerm name;
        Expr e;
        ATerm pos;
        if (!matchBind(*i, name, e, pos)) abort(); /* can't happen */
        if (names.get(name))
            throw Error(format("duplicate attribute `%1%' at %2%")
                % aterm2String(name) % showPos(pos));
        names.set(name, name);
    }
}


static void checkAttrSets(ATerm e)
{
    ATermList formals;
    ATerm body, pos;
    if (matchFunction(e, formals, body, pos)) {
        ATermMap names;
        for (ATermIterator i(formals); i; ++i) {
            ATerm name;
            Expr deflt;
            if (!matchNoDefFormal(*i, name) &&
                !matchDefFormal(*i, name, deflt))
                abort();
            if (names.get(name))
                throw Error(format("duplicate formal function argument `%1%' at %2%")
                    % aterm2String(name) % showPos(pos));
            names.set(name, name);
        }
    }

    ATermList bnds;
    if (matchAttrs(e, bnds)) {
        ATermMap names;
        checkAttrs(names, bnds);
    }
    
    ATermList rbnds, nrbnds;
    if (matchRec(e, rbnds, nrbnds)) {
        ATermMap names;
        checkAttrs(names, rbnds);
        checkAttrs(names, nrbnds);
    }
    
    if (ATgetType(e) == AT_APPL) {
        int arity = ATgetArity(ATgetAFun(e));
        for (int i = 0; i < arity; ++i)
            checkAttrSets(ATgetArgument(e, i));
    }

    else if (ATgetType(e) == AT_LIST)
        for (ATermIterator i((ATermList) e); i; ++i)
            checkAttrSets(*i);
}


static Expr parse(EvalState & state,
    const char * text, const Path & path,
    const Path & basePath)
{
    yyscan_t scanner;
    ParseData data;
    data.basePath = basePath;
    data.path = path;

    yylex_init(&scanner);
    yy_scan_string(text, scanner);
    int res = yyparse(scanner, &data);
    yylex_destroy(scanner);
    
    if (res) throw Error(data.error);

    try {
        checkVarDefs(state.primOps, data.result);
    } catch (Error & e) {
        throw Error(format("%1%, in `%2%'") % e.msg() % path);
    }
    
    checkAttrSets(data.result);

    return data.result;
}


Expr parseExprFromFile(EvalState & state, Path path)
{
    SwitchToOriginalUser sw;

    assert(path[0] == '/');

#if 0
    /* Perhaps this is already an imploded parse tree? */
    Expr e = ATreadFromNamedFile(path.c_str());
    if (e) return e;
#endif

    /* If `path' is a symlink, follow it.  This is so that relative
       path references work. */
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting status of `%1%'") % path);
    if (S_ISLNK(st.st_mode)) path = absPath(readLink(path), dirOf(path));

    /* If `path' refers to a directory, append `/default.nix'. */
    if (stat(path.c_str(), &st))
        throw SysError(format("getting status of `%1%'") % path);
    if (S_ISDIR(st.st_mode))
        path = canonPath(path + "/default.nix");

    /* Read the input file.  We can't use SGparseFile() because it's
       broken, so we read the input ourselves and call
       SGparseString(). */
    AutoCloseFD fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) throw SysError(format("opening `%1%'") % path);

    if (fstat(fd, &st) == -1)
        throw SysError(format("statting `%1%'") % path);

    char text[st.st_size + 1];
    readFull(fd, (unsigned char *) text, st.st_size);
    text[st.st_size] = 0;

    return parse(state, text, path, dirOf(path));
}


Expr parseExprFromString(EvalState & state,
    const string & s, const Path & basePath)
{
    return parse(state, s.c_str(), "(string)", basePath);
}
