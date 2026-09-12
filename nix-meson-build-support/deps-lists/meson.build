# These are private dependencies with pkg-config files. What private
# means is that the dependencies are used by the library but they are
# *not* used (e.g. `#include`-ed) in any installed header file, and only
# in regular source code (`*.cc`) or private, uninstalled headers. They
# are thus part of the *implementation* of the library, but not its
# *interface*.
#
# See `man pkg-config` for some details.
deps_private = []

# These are public dependencies with pkg-config files. Public is the
# opposite of private: these dependencies are used in installed header
# files. They are part of the interface (and implementation) of the
# library.
#
# N.B. This concept is mostly unrelated to our own concept of a public
# (stable) API, for consumption outside of the Nix repository.
# `libnixutil` is an unstable C++ library, whose public interface is
# likewise unstable. `libutilc` conversely is a hopefully-soon stable
# C library, whose public interface --- including public but not private
# dependencies --- will also likewise soon be stable.
#
# N.B. For distributions that care about "ABI" stability and not just
# "API" stability, the private dependencies also matter as they can
# potentially affect the public ABI.
deps_public = []

# These are subproject deps (type == "internal"). They are other
# packages in `/src` in this repo. The private vs public distinction is
# the same as above.
deps_private_subproject = []
deps_public_subproject = []

# These are dependencies without pkg-config files (Boost, or ad-hoc
# libraries from `find_library`). The private ones only link this library;
# the public ones are also propagated to consumers within this build, but
# cannot be expressed in the generated pkg-config file.
deps_private_no_pkgconfig = []
deps_public_no_pkgconfig = []
