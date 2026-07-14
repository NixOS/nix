#pragma once

#include <unistd.h>
#include <cxxabi.h>
#include <stdexcept>
#include <typeinfo>
#include <cstring>
#include <cstdio>

#ifndef CXA_THROW_ON_LOGIC_ERROR
#  define CXA_THROW_ON_LOGIC_ERROR() abort()
#endif

#include <cxxabi.h>

#ifndef __GLIBCXX__
/* libc++abi implements the Itanium ABI RTTI classes but, unlike
   libstdc++, doesn't declare them in <cxxabi.h>. Their layout is
   fixed by the ABI, so declare what we need. */
namespace __cxxabiv1 {
class __class_type_info : public std::type_info
{
public:
    ~__class_type_info() override;
};

class __si_class_type_info : public __class_type_info
{
public:
    const __class_type_info * __base_type;
};
} // namespace __cxxabiv1
#endif

static bool is_logic_error(const std::type_info * tinfo)
{
    if (*tinfo == typeid(std::logic_error))
        return true;

    auto * si = dynamic_cast<const __cxxabiv1::__si_class_type_info *>(tinfo);
    if (si)
        return is_logic_error(si->__base_type);

    return false;
}

static void abort_on_exception(void * exc, const std::type_info * tinfo)
{
    if (!is_logic_error(tinfo))
        return;

    char buf[512];
    snprintf(
        buf,
        sizeof(buf),
        "Aborting on unexpected exception of type '%s', error: %s\n",
        tinfo->name(),
        ((std::exception *) exc)->what());
    [[maybe_unused]] auto r = write(STDERR_FILENO, buf, strlen(buf));

    CXA_THROW_ON_LOGIC_ERROR();
}
