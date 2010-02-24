
#ifndef _LIBTERM_TERM_FWD_HH
# define _LIBTERM_TERM_FWD_HH

# include "grammar.hh"

// Types prefixed by an 'A' are the terms which should be manipulated by the
// user.  The 'A' letter stands for Abstract.  Their content should be
// limited to a pointer and they must not use any virtual keyword.  On the
// other hand, the implementation of terms can declare more than one
// attribute and use virtual keywords as long as they don't cost too much.

namespace term
{
  class ATerm;
  class ATermImpl; // should be renamed to TermImpl

# define TRM_DECLARE(Name, Base, Attributes, BaseArgs)  \
  class A ## Name;                                      \
  class Name;

  TRM_GRAMMAR_NODES(TRM_DECLARE, TRM_DECLARE)

# undef TRM_DECLARE

}

#endif
