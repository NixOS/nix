#
# Old configure.ac
#

project(
  'nix',
  'cpp',
  default_options : [
    'cpp_std=c++14',
    #'localstatedir=/nix/var',
  ],
)

sed = find_program('sed')

conf_data = configuration_data()

system = get_option('with-system')
if system == ''
  system = host_machine.cpu() + '-' + host_machine.system()
endif
conf_data.set_quoted('SYSTEM', system, description : 'platform identifier (`cpu-os\')')

# State should be stored in /nix/var, unless the user overrides it explicitly.
if get_option('normal-var')
  nixstatedir = '/nix/var'
else
  nixstatedir = get_option('localstatedir')
endif

global_libs = []
# Solaris-specific stuff.
if host_machine.cpu() == 'sunos'
  global_libs += [
    dependency('socket'),
    dependency('nsl'),
  ]
endif

# Use 64-bit file system calls so that we can support files > 2 GiB.

# Note vs configure.ac: Meson always does large file support.

# Check for pubsetbuf.
want_pubsetbuf = get_option('have-pubsetbuf')
if want_pubsetbuf.disabled()
  have_pubsetbuf = false
else
  have_pubsetbuf = meson.get_compiler('cpp').compiles(
    '''
      #include <iostream>
      using namespace std;
      static char buf[1024];
      void func() {
        cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
      }
    ''',
    name : 'pubsetbuf'
  )
  if want_pubsetbuf.enabled() and not have_pubsetbuf
    error('you required the `pubsetbuf` function, but we do not have it.')
  endif
endif
if have_pubsetbuf
  conf_data.set('HAVE_PUBSETBUF', 1, description : 'Whether pubsetbuf is available.')
endif

foreach f : [
  'statvfs',
  'pipe2',
  # Check for lutimes, optionally used for changing the mtime of symlinks.
  'lutimes',
]
  if meson.get_compiler('cpp').has_function(f)
    conf_data.set('HAVE_' + f.to_upper(), 1)
  endif
endforeach

# Check whether the store optimiser can optimise symlinks.
want_link_symlink = get_option('can-link-symlink')
if want_link_symlink.disabled()
  can_link_symlink = true
else
  link_text_script = '''
    ln -s bla tmp_link;
    ln tmp_link tmp_link2 2> /dev/null;
    rm -f tmp_link tmp_link2
  '''
  can_link_symlink = run_command('sh', '-c', link_text_script).returncode() == 0
  if want_link_symlink.enabled() and not can_link_symlink
    error('you required that we hard link symlinks, but we cannot.')
  endif
endif
if can_link_symlink
  conf_data.set('CAN_LINK_SYMLINK', 1, description : 'Whether link() works on symlinks.')
endif

# Check for <locale>.
meson.get_compiler('cpp').check_header('locale')

foreach prog : [
  'bash',
  'patch',
  'sed',
  'tar',
  'bzip2',
  'gzip',
  'xz',
  'cat',
  'tr',
]
  find_program(prog)
endforeach

false_ = find_program('false')
foreach prog : [
  'xmllint',
  'xsltproc',
  'flex',
  'bison',
]
  find_program(prog, required: false)
endforeach

find_program('dot', required: false)
lsof = find_program('lsof', required: false)

conf_data.set_quoted('coreutils', get_option('with-coreutils-bin'))
storedir = get_option('with-store-dir')

# Special case
pthread = dependency('threads')

# Look for OpenSSL, a required dependency.
openssl = [
  dependency('openssl'),
  dependency('libcrypto'),
]

# Look for libbz2, a required dependency.
bz2 = meson.get_compiler('cpp').find_library('bz2')

# Look for SQLite, a required dependency.
sqlite3 = dependency('sqlite3', version : '>= 3.6.19')

# Look for libcurl, a required dependency.
libcurl = dependency('libcurl')

# Look for editline, a required dependency.
editline = dependency('libeditline')

# Look for libsodium, an optional dependency.
libsodium = dependency('sodium', required : false)
if libsodium.found()
  conf_data.set('HAVE_SODIUM', 1, description : 'Whether to use libsodium for cryptography.')
endif

# Look for libbrotli{enc,dec}.
libbrotli = [
  dependency('libbrotlienc'),
  dependency('libbrotlidec'),
]

# Look for liblzma, a required dependency.
liblzma = dependency('liblzma')
if meson.get_compiler('cpp').has_function('lzma_stream_encoder_mt', dependencies : liblzma)
  conf_data.set('HAVE_LSMA_MT', 1, description : 'xz multithreaded compression support')
endif

# Look for libseccomp, required for Linux sandboxing.
want_seccomp = get_option('enable-seccomp-sandboxing')
if want_seccomp.disabled() or (want_seccomp.auto() and host_machine.system() != 'linux')
  seccomp = dependency('', required : false)
else # enabled or auto on linux
  seccomp = dependency('libseccomp')
  if want_seccomp.enabled() and not seccomp.found()
    error('you required `libseccomp`, but we do not have it.')
  endif
endif
if seccomp.found()
  conf_data.set('HAVE_SECCOMP', 1, description : 'Whether seccomp is available and should be used for sandboxing.')
endif

# Look for aws-cpp-sdk-s3.
want_s3 = get_option('enable-s3')
if want_s3.disabled()
  enable_s3 = false
else
  enable_s3 = meson.get_compiler('cpp').check_header('aws/s3/S3Client.h')
endif
if enable_s3
  conf_data.set('ENABLE_S3', 1, description : 'Whether to enable S3 support via aws-sdk-cpp.')
endif

if enable_s3
  aws_version = meson.get_compiler('cpp').get_define(
    'AWS_SDK_VERSION_STRING',
    prefix : '#include <aws/core/VersionConfig.h>'
  ).strip('"').split('.')
  conf_data.set('AWS_VERSION_MAJOR', aws_version[0], description : 'Major version of aws-sdk-cpp.')
  conf_data.set('AWS_VERSION_MINOR', aws_version[1], description : 'Minor version of aws-sdk-cpp.')
endif

# Whether to use the Boehm garbage collector.
want_gc = get_option('enable-gc')
if want_gc.disabled()
  bdw_gc = false
else
  bdw_gc = dependency('bdw-gc', required : false)
  if want_gc.enabled() and not bdw_gc.found()
    error('you required garbage collection, but we do not have the library to support it.')
  endif
endif
if libsodium.found()
  conf_data.set('HAVE_BOEHMGC', 1, description : 'Whether to use the Boehm garbage collector.')
endif

# documentation generation switch
conf_data.set('doc_generate', not get_option('disable-doc-gen'))


foreach f : [
  # Setuid installations.
  'setresuid',
  'setreuid',
  'lchown',
  # Nice to have, but not essential.
  'strsignal',
  'posix_fallocate',
  'sysconf',
]
  if meson.get_compiler('cpp').has_function(f)
    conf_data.set('HAVE_' + f.to_upper(), 1)
  endif
endforeach

# This is needed if bzip2 is a static library, and the Nix libraries
# are dynamic.
if bz2.found() and host_machine.system() == 'darwin'
  add_project_link_arguments('-all_load')
endif

# Do we have GNU tar?
tar_script = '''
  if @t0@ --version 2> /dev/null | grep -q GNU && tar cvf /dev/null --warning=no-timestamp ./config.log > /dev/null
'''.format(find_program('tar').path())
tar_flags = ''
if run_command('sh', '-c', link_text_script).returncode() == 0
  tar_flags += '--warning=no-timestamp'
endif
conf_data.set_quoted('tarFlags', tar_flags)

sandbox_shell = get_option('with-sandbox-shell')

#
# Extra Configure
#

boost_context = dependency('boost', modules : ['context'])

# Done by default by autoconf
conf_data.set_quoted('PACKAGE_VERSION', meson.project_version())

if enable_s3
  aws_cpp_sdk_transfer = meson.get_compiler('cpp').find_library('aws-cpp-sdk-transfer')
  aws_cpp_sdk_s3 = meson.get_compiler('cpp').find_library('aws-cpp-sdk-s3')
  aws_cpp_sdk_core = meson.get_compiler('cpp').find_library('aws-cpp-sdk-core')
else
  aws_cpp_sdk_transfer = dependency('', required : false)
  aws_cpp_sdk_s3 = dependency('', required : false)
  aws_cpp_sdk_core = dependency('', required : false)
endif

#
# original Configure.ac, but much go last
#

configure_file(
  output : 'config.h',
  configuration : conf_data
)

#
# Old Makefile
#

global_subdirs = include_directories([
  '.',
  'src',
  'src/libutil',
  'src/libstore',
  'src/libmain',
  'src/libexpr',
  'src/nix',
])

add_project_arguments('-g', '-Wall', '-include', 'config.h', language : 'cpp')

libraries = []

subdir('src/libutil')
subdir('src/libstore')
subdir('src/libmain')
subdir('src/libexpr')
