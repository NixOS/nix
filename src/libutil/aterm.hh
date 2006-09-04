#ifndef __ATERM_H
#define __ATERM_H

extern "C" {
#include <aterm2.h>
}

#include "types.hh"


namespace nix {


/* Print an ATerm. */
string atPrint(ATerm t);

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


/* Convert strings to ATerms. */
ATerm toATerm(const char * s);
ATerm toATerm(const string & s);

 
}


/* Write an ATerm to an output stream. */
std::ostream & operator << (std::ostream & stream, ATerm e);


#endif /* !__ATERM_H */
