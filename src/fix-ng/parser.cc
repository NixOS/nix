#include <sstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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


/* Cleanup cleans up an imploded parse tree into an actual abstract
   syntax tree that we can evaluate.  It removes quotes around
   strings, converts integer literals into actual integers, and
   absolutises paths relative to the directory containing the input
   file. */
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
            return ATmake("Path(<str>)", canonPath(path).c_str());
        }

        if (ATmatch(e, "Int(<str>)", &s)) {
            istringstream s2(s);
            int n;
            s2 >> n;
            return ATmake("Int(<int>)", n);
        }

        if (ATmatch(e, "Bool(\"true\")", &s))
            return ATmake("Bool(True)");
        
        if (ATmatch(e, "Bool(\"false\")", &s))
            return ATmake("Bool(False)");

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

    /* Parse it. */
    ATerm result = SGparseString(lang, "Expr", text);
    if (!result)
        throw SysError(format("parse failed in `%1%'") % path);
    if (SGisParseError(result))
        throw Error(format("parse error in `%1%': %2%")
            % path % printTerm(result));

    /* Implode it. */
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

    /* Finally, clean it up. */
    Cleanup cleanup;
    cleanup.basePath = dirOf(path);
    return bottomupRewrite(cleanup, imploded);
}
