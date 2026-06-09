#if defined(__has_attribute)
#  if __has_attribute(no_profile_instrument_function)
#    define NIX_NO_PROFILE_INSTRUMENT_FUNCTION __attribute__((no_profile_instrument_function))
#  endif
#endif

#ifndef NIX_NO_PROFILE_INSTRUMENT_FUNCTION
#  define NIX_NO_PROFILE_INSTRUMENT_FUNCTION
#endif

// This ASan hook is linked into many instrumented binaries and libraries. Do
// not emit coverage counters for it because repeated weak definitions of the
// hook can produce profile data that llvm-profdata treats as corrupted.
extern "C" [[gnu::retain, gnu::weak]] NIX_NO_PROFILE_INSTRUMENT_FUNCTION const char * __asan_default_options()
{
    // We leak a bunch of memory knowingly on purpose. It's not worthwhile to
    // diagnose that memory being leaked for now.
    return "abort_on_error=1:print_summary=1:detect_leaks=0:detect_odr_violation=0";
}
