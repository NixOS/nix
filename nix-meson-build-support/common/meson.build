# This is only conditional to work around
# https://github.com/mesonbuild/meson/issues/13293. It should be
# unconditional.
if not (host_machine.system() == 'windows' and cxx.get_id() == 'gcc')
  deps_private += dependency('threads')
endif

if host_machine.system() == 'cygwin'
  # -std=gnu on cygwin defines 'unix', which conflicts with the namespace
  add_project_arguments(
    '-D_POSIX_C_SOURCE=200809L',
    '-D_GNU_SOURCE',
    language : 'cpp',
  )
endif

add_project_arguments(
  '-Wdeprecated-copy',
  '-Werror=suggest-override',
  '-Werror=switch',
  '-Werror=switch-enum',
  '-Werror=undef',
  '-Werror=unused-result',
  '-Werror=sign-compare',
  '-Werror=return-type',
  '-Werror=non-virtual-dtor',
  '-Wignored-qualifiers',
  '-Wimplicit-fallthrough',
  '-Wno-deprecated-declarations',
  language : 'cpp',
)

# GCC doesn't benefit much from precompiled headers.
do_pch = cxx.get_id() == 'clang'

if cxx.get_id() == 'gcc'
  add_project_arguments(
    '-Wno-interference-size', # Used for C++ ABI only. We don't provide any guarantees about different march tunings.
    language : 'cpp',
  )
endif

# This is a clang-only option for improving build times.
# It forces the instantiation of templates in the PCH itself and
# not every translation unit it's included in.
# It's available starting from clang 11, which is old enough to not
# bother checking the version.
# This feature helps in particular with the expensive nlohmann::json template
# instantiations in libutil and libstore.
if cxx.get_id() == 'clang'
  add_project_arguments('-fpch-instantiate-templates', language : 'cpp')
  # Catch brace elision bugs: when WorkerProto::Version changed from `unsigned int`
  # to `struct { unsigned int major; uint8_t minor; }`, `.version = 16` silently
  # became `.version = {16, 0}` instead of failing, breaking protocol compatibility
  # in a subtle way
  add_project_arguments('-Werror=c99-designator', language : 'cpp')
endif

# Detect if we're using libstdc++ (GCC's standard library)
# libstdc++ uses Intel TBB as backend for C++17 parallel algorithms when <execution> is included.
# boost::concurrent_flat_map includes <execution>, which would require linking against TBB.
# Since we don't actually use parallel algorithms, disable the TBB backend to avoid the dependency.
# TBB is a dependency of blake3 and leaking into our build environment.
is_using_libstdcxx = cxx.compiles(
  '''
  #include <ciso646>
  #ifndef __GLIBCXX__
  #error "not libstdc++"
  #endif
  int main() { return 0; }
''',
  name : 'using libstdc++',
)

if is_using_libstdcxx
  add_project_arguments('-D_GLIBCXX_USE_TBB_PAR_BACKEND=0', language : 'cpp')
endif

# Darwin ld doesn't like "X.Y.ZpreABCD+W"
nix_soversion = meson.project_version().split('+')[0].split('pre')[0]

# Clang does not support prelinking on static builds
if cxx.get_id() == 'clang' and get_option('default_library') == 'static'
  prelink = false
else
  prelink = true
endif

subdir('assert-fail')
subdir('asan-options')
