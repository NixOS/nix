#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include <sglr.h>
#include <asfix2.h>
}

#include "parser.hh"
#include "shared.hh"
#include "fix-expr.hh"
#include "expr.hh"
#include "parse-table.h"


struct Cleanup : TermFun
{
    string basePath;

    virtual ATerm operator () (ATerm e)
    {
        char * s;

        if (ATmatch(e, "Str(<str>)", &s)) {
            string s2(s);
            return ATmake("Str(<str>)",
                string(s2, 1, s2.size() - 2).c_str());
        }

        if (ATmatch(e, "Path(<str>)", &s)) {
            string path(s);
            if (path[0] != '/')
                path = basePath + "/" + path;
            return ATmake("Str(<str>)", canonPath(path).c_str());
        }

        if (ATmatch(e, "Int(<str>)", &s)) {
            istringstream s2(s);
            int n;
            s2 >> n;
            return ATmake("Int(<int>)", n);
        }

        return e;
    }
};


Expr parseExprFromFile(Path path)
{
#if 0
    /* Perhaps this is already an imploded parse tree? */
    Expr e = ATreadFromNamedFile(path.c_str());
    if (e) return e;
#endif

    /* If `path' refers to a directory, append `/default.fix'. */
    struct stat st;
    if (stat(path.c_str(), &st))
        throw SysError(format("getting status of `%1%'") % path);
    if (S_ISDIR(st.st_mode))
        path = canonPath(path + "/default.fix");

    /* Initialise the SDF libraries. */
    static bool initialised = false;
    static ATerm parseTable = 0;
    static language lang = 0;

    if (!initialised) {
        PT_initMEPTApi();
        PT_initAsFix2Api();
        SGinitParser(ATfalse);

        ATprotect(&parseTable);
        parseTable = ATreadFromBinaryString(
            (char *) fixParseTable, sizeof fixParseTable);
        if (!parseTable)
            throw Error(format("cannot construct parse table term"));

        ATprotect(&lang);
        lang = ATmake("Fix");
        if (!SGopenLanguageFromTerm(
                (char *) programId.c_str(), lang, parseTable))
            throw Error(format("cannot open language"));

        SG_STARTSYMBOL_ON();
        SG_OUTPUT_ON();
        SG_ASFIX2ME_ON();
        SG_AMBIGUITY_ERROR_ON();

        initialised = true;
    }

    ATerm result = SGparseFile((char *) programId.c_str(), lang,
        "Expr", (char *) path.c_str());
    if (!result)
        throw SysError(format("parse failed in `%1%'") % path);
    if (SGisParseError(result))
        throw Error(format("parse error in `%1%': %2%")
            % path % printTerm(result));

    PT_ParseTree tree = PT_makeParseTreeFromTerm(result);
    if (!tree)
        throw Error(format("cannot create parse tree"));
    
    ATerm imploded = PT_implodeParseTree(tree,
        ATtrue,
        ATtrue,
        ATtrue,
        ATtrue,
        ATtrue,
        ATtrue,
        ATfalse,
        ATtrue,
        ATtrue,
        ATtrue,
        ATfalse);
    if (!imploded)
        throw Error(format("cannot implode parse tree"));

    Cleanup cleanup;
    cleanup.basePath = dirOf(path);
    return bottomupRewrite(cleanup, imploded);
}
