
#ifndef _LIBTERM_VISITOR_FWD_HH
# define _LIBTERM_VISITOR_FWD_HH

# include "term_fwd.hh"

// Types prefixed by an 'A' are the terms which should be manipulated by the
// user.  The 'A' letter stands for Abstract.  Their content should be
// limited to a pointer and they must not use any virtual keyword.  On the
// other hand, the implementation of terms can declare more than one
// attribute and use virtual keywords as long as they don't cost too much.

namespace term
{
  // base class for visitor implementation.
  class ATermVisitor;
}

#endif
