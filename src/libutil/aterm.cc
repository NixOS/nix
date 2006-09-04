#include "aterm.hh"

using std::string;


string nix::atPrint(ATerm t)
{
    if (!t) throw Error("attempt to print null aterm");
    char * s = ATwriteToString(t);
    if (!s) throw Error("cannot print term");
    return s;
}


std::ostream & operator << (std::ostream & stream, ATerm e)
{
    return stream << nix::atPrint(e);
}


nix::Error nix::badTerm(const format & f, ATerm t)
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


ATerm nix::toATerm(const char * s)
{
    return (ATerm) ATmakeAppl0(ATmakeAFun((char *) s, 0, ATtrue));
}


ATerm nix::toATerm(const string & s)
{
    return toATerm(s.c_str());
}
