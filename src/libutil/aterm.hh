#ifndef __ATERM_H
#define __ATERM_H

extern "C" {
#include <aterm2.h>
}

#include "util.hh"


/* Print an ATerm. */
string atPrint(ATerm t);

/* Write an ATerm to an output stream. */
ostream & operator << (ostream & stream, ATerm e);

class ATermIterator
{
    ATermList t;

public:
    ATermIterator(ATermList _t) : t(_t) { }
    ATermIterator & operator ++ ()
    {
        t = ATgetNext(t);
        return *this;
    }
    ATerm operator * ()
    {
        return ATgetFirst(t);
    }
    operator bool ()
    {
        return t != ATempty;
    }
};


/* Type-safe matching. */

struct ATMatcher 
{
    ATerm t;
    int pos;
    const static int failPos = -2;
    const static int funPos = -1;

    ATMatcher() : t(0), pos(failPos)
    {
    }

    operator bool() const
    {
        return pos != failPos;
    }
};

/* Initiate matching of a term. */
ATMatcher & atMatch(ATMatcher & pos, ATerm t);

/* Get the next argument of an application. */
ATMatcher & operator >> (ATMatcher & pos, ATerm & out);

/* Get the name of the function symbol of an applicatin, or the next
   argument of an application as a string. */
ATMatcher & operator >> (ATMatcher & pos, string & out);

/* Like the previous, but check that the string is equal to the given
   string. */
ATMatcher & operator >> (ATMatcher & pos, const string & s);

/* Get the next argument of an application, and verify that it is a
   list. */
ATMatcher & operator >> (ATMatcher & pos, ATermList & out);


/* Throw an exception with an error message containing the given
   aterm. */
Error badTerm(const format & f, ATerm t);


#endif /* !__ATERM_H */
