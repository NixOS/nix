#pragma once

#include<variant>

namespace nix {

/* A These<A,B> is like a std::variant<A,B>, but it also contemplates the
   possibility that we have both values. It takes the name from the analogous
   haskell pattern */

template <typename A, typename B>
using These = typename std::variant<A, B, std::pair<A,B>>;

}
