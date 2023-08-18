# nix check functions
#============================================================================


# check for required functions
#--------------------------------------------------
required_functions = [
    'lutimes',
    'lchown',
    'pipe2',
    'posix_fallocate',
    'setresuid',
    'setreuid',
    'statvfs',
    'strsignal',
    'sysconf']

foreach f : required_functions
    if cpp.has_function(f)
        config_h.set(
            'HAVE_@0@'.format(f.to_upper()), 1,
            description : 'Define to 1 if you have the `@0@` function.'.format(f))
    endif
endforeach


# compiler check for pubsetbuff
#--------------------------------------------------
pubsetbuff_c = '''
#include <iostream>
using namespace std;
static char buf[1024];
void func() {
cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
}'''

check_pubsetbuff = meson.get_compiler('cpp').compiles(pubsetbuff_c, name : 'pubsetbuf')
config_h.set(
    'HAVE_PUBSETBUF', check_pubsetbuff.to_int(),
    description : 'Define to 1 if you have the `pubsetbuf` function.')
