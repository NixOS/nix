#include "aterm.hh"


string atPrint(ATerm t)
{
    if (!t) throw Error("attempt to print null aterm");
    char * s = ATwriteToString(t);
    if (!s) throw Error("cannot print term");
    return s;
}


ostream & operator << (ostream & stream, ATerm e)
{
    return stream << atPrint(e);
}


ATMatcher & atMatch(ATMatcher & pos, ATerm t)
{
    pos.t = t;
    pos.pos = ATMatcher::funPos;
    return pos;
}


static inline bool failed(const ATMatcher & pos)
{
    return pos.pos == ATMatcher::failPos;
}


static inline ATMatcher & fail(ATMatcher & pos)
{
    pos.pos = ATMatcher::failPos;
    return pos;
}


ATMatcher & operator >> (ATMatcher & pos, ATerm & out)
{
    out = 0;
    if (failed(pos)) return pos;
    if (pos.pos == ATMatcher::funPos || 
        ATgetType(pos.t) != AT_APPL ||
        pos.pos >= (int) ATgetArity(ATgetAFun(pos.t)))
        return fail(pos);
    out = ATgetArgument(pos.t, pos.pos);
    pos.pos++;
    return pos;
}


ATMatcher & operator >> (ATMatcher & pos, string & out)
{
    out = "";
    if (pos.pos == ATMatcher::funPos) {
        if (ATgetType(pos.t) != AT_APPL) return fail(pos);
        out = ATgetName(ATgetAFun(pos.t));
        pos.pos = 0;
    } else {
        ATerm t;
        pos = pos >> t;
        if (failed(pos)) return pos;
        if (ATgetType(t) != AT_APPL ||
            ATgetArity(ATgetAFun(t)) != 0)
            return fail(pos);
        out = ATgetName(ATgetAFun(t));
    }
    return pos;
}


ATMatcher & operator >> (ATMatcher & pos, const string & s)
{
    string s2;
    pos = pos >> s2;
    if (failed(pos)) return pos;
    if (s != s2) return fail(pos);
    return pos;
}


ATMatcher & operator >> (ATMatcher & pos, int & n)
{
    n = 0;
    ATerm t;
    pos = pos >> t;
    if (failed(pos)) return pos;
    if (ATgetType(t) != AT_INT) return fail(pos);
    n = ATgetInt((ATermInt) t);
    return pos;
}


ATMatcher & operator >> (ATMatcher & pos, ATermList & out)
{
    out = 0;
    ATerm t;
    pos = pos >> t;
    if (failed(pos)) return pos;
    if (ATgetType(t) != AT_LIST) return fail(pos);
    out = (ATermList) t;
    return pos;
}


Error badTerm(const format & f, ATerm t)
{
    char * s = ATwriteToString(t);
    if (!s) throw Error("cannot print term");
    if (strlen(s) > 1000) {
        int len;
        s = ATwriteToSharedString(t, &len);
        if (!s) throw Error("cannot print term");
    }
    return Error(format("%1%, in `%2%'") % f.str() % (string) s);
}
