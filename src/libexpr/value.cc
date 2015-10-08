#include "value.hh"
#include "util.hh"

#include <new>

#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#if HAVE_BOEHMGC

#include <gc/gc.h>
#include <gc/gc_cpp.h>

#define NEW new (UseGC)

#else

#define NEW new

#endif


namespace nix {


static char * dupString(const char * s)
{
    char * t;
#if HAVE_BOEHMGC
    t = GC_strdup(s);
#else
    t = strdup(s);
#endif
    if (!t) throw std::bad_alloc();
    return t;
}


static void * allocBytes(size_t n)
{
    void * p;
#if HAVE_BOEHMGC
    p = GC_malloc(n);
#else
    p = malloc(n);
#endif
    if (!p) throw std::bad_alloc();
    return p;
}


void Value::setString(const char * s)
{
    setStringNoCopy(dupString(s), nullptr);
}


void Value::setString(const string & s, const PathSet & context)
{
    const char * str = dupString(s.c_str());
    const char * * ctx = nullptr;
    if (!context.empty()) {
        unsigned int n = 0;
        ctx = (const char * *)
            allocBytes((context.size() + 1) * sizeof(char *));
        for (auto & i : context)
            ctx[n++] = dupString(i.c_str());
        ctx[n] = nullptr;
    }
    setStringNoCopy(str, ctx);
}


void Value::setPath(const char * s)
{
    setPathNoCopy(dupString(s));
}


}
