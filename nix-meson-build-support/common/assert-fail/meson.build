can_wrap_assert_fail_test_code = '''
#include <cstdlib>
#include <cassert>

int main()
{
    assert(0);
}

extern "C" void * __real___assert_fail(const char *, const char *, unsigned int, const char *);

extern "C" void *
__wrap___assert_fail(const char *, const char *, unsigned int, const char *)
{
    return __real___assert_fail(nullptr, nullptr, 0, nullptr);
}
'''

wrap_assert_fail_args = [ '-Wl,--wrap=__assert_fail' ]

can_wrap_assert_fail = cxx.links(
  can_wrap_assert_fail_test_code,
  args : wrap_assert_fail_args,
  name : 'linker can wrap __assert_fail',
)

if can_wrap_assert_fail
  deps_other += declare_dependency(
    sources : 'wrap-assert-fail.cc',
    link_args : wrap_assert_fail_args,
  )
endif
