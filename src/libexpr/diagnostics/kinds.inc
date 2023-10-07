/// diagnostics/kinds.inc, provides declarations for diagnostics.

// provides: DIAG_SIMPLE(SNAME, CNAME, SEVERITY, MESSAGE)
// provides: DIAG_BODY(SNAME, CNAME, SEVERITY, BODY)
// provides: DIAG_NOTE_SIMPLE(SNAME, CNAME, SEVERITY, MESSAGE)
// provides: DIAG_NOTE_BODY(SNAME, CNAME, SEVERITY, BODY)

//=============================================================================/
// DIAG_SIMPLE(SName, ClassName, Severity, Message)
// "simple" means they have no additional body
//=============================================================================/

#ifdef DIAG_SIMPLE
DIAG_SIMPLE("path-trailing-slash", PathTrailingSlash, Error, "path has a trailing slash")
DIAG_SIMPLE("dynamic-in-let", DynamicInLet, Error, "dynamic attributes not allowed in let")
DIAG_SIMPLE("dynamic-in-inherit", InheritDynamic, Error, "dynamic attributes not allowed in inherit")
DIAG_SIMPLE("url-literal", URLLiteral, Warning, "url literals are disabled")
DIAG_SIMPLE("hpath-impure", HPath, Error, "hpath cannot be resolved in pure mode")
#endif

#define COMMON_BODY \
    Severity severity() const override; \
    Kind kind() const override; \
    std::string_view format() const override; \
    std::string_view sname() const override;

//=============================================================================/
// DIAG_BODY(SName, ClassName, Severity, Body)
// diagnostics with special body
//=============================================================================/
#ifdef DIAG_BODY
DIAG_BODY("bison", BisonParse, Fatal, {
    std::string err;
    COMMON_BODY
})
DIAG_BODY("invalid-integer", InvalidInteger, Fatal, {
    std::string text;
    COMMON_BODY
    DiagInvalidInteger(PosIdx loc, std::string text);
})

DIAG_BODY("invalid-float", InvalidFloat, Fatal, {
    std::string text;
    COMMON_BODY
    DiagInvalidFloat(PosIdx loc, std::string text);
})

#undef COMMON_BODY
#endif // DIAG_BODY
