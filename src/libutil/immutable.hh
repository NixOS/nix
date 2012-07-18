#pragma once

#include <types.hh>

namespace nix {

/* Make the given path immutable, i.e., prevent it from being modified
   in any way, even by root.  This is a no-op on platforms that do not
   support this, or if the calling user is not privileged.  On Linux,
   this is implemented by doing the equivalent of ‘chattr +i path’. */
void makeImmutable(const Path & path);

/* Make the given path mutable. */
void makeMutable(const Path & path);

}
