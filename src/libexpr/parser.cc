#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "aterm.hh"
#include "parser.hh"


struct ParseData 
{
    Expr result;
    string basePath;
    string location;
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
    return string2ATerm(absPath(aterm2String(t), data->basePath).c_str());
}
    
void parseError(ParseData * data, char * error, int line, int column)
{
    data->error = (format("%1%, at line %2%, column %3%, of %4%")
        % error % line % column % data->location).str();
}
        
ATerm fixAttrs(int recursive, ATermList as)
{
    ATMatcher m;
    ATermList bs = ATempty, cs = ATempty;
    ATermList * is = recursive ? &cs : &bs;
    for (ATermIterator i(as); i; ++i) {
        ATermList names;
        if (atMatch(m, *i) >> "Inherit" >> names)
            for (ATermIterator j(names); j; ++j)
                *is = ATinsert(*is,
                    ATmake("Bind(<term>, Var(<term>))", *j, *j));
        else bs = ATinsert(bs, *i);
    }
    if (recursive)
        return ATmake("Rec(<term>, <term>)", bs, cs);
    else
        return ATmake("Attrs(<term>)", bs);
}

int yyparse(yyscan_t scanner, ParseData * data);
}


static Expr parse(EvalState & state,
    const char * text, const string & location,
    const Path & basePath)
{
    yyscan_t scanner;
    ParseData data;
    data.basePath = basePath;
    data.location = location;

    yylex_init(&scanner);
    yy_scan_string(text, scanner);
    int res = yyparse(scanner, &data);
    yylex_destroy(scanner);
    
    if (res) throw Error(data.error);

    try {
        checkVarDefs(state.primOpsAll, data.result);
    } catch (Error & e) {
        throw Error(format("%1%, in %2%") % e.msg() % location);
    }

    return data.result;
}


Expr parseExprFromFile(EvalState & state, Path path)
{
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

    return parse(state, text, "`" + path + "'", dirOf(path));
}


Expr parseExprFromString(EvalState & state,
    const string & s, const Path & basePath)
{
    return parse(state, s.c_str(), "(string)", basePath);
}
