extern "C" [[gnu::retain, gnu::weak]] const char * __asan_default_options()
{
    // We leak a bunch of memory knowingly on purpose. It's not worthwhile to
    // diagnose that memory being leaked for now.
    return "abort_on_error=1:print_summary=1:detect_leaks=0:detect_odr_violation=0";
}
