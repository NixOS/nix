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


/* Throw an exception with an error message containing the given
   aterm. */
Error badTerm(const format & f, ATerm t);


#endif /* !__ATERM_H */
