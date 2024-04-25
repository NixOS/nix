# Nix-Perl Meson build
#============================================================================


# init project
#============================================================================
project (
  'nix-perl',
  'cpp',
  meson_version : '>= 0.64.0',
  license : 'LGPL-2.1-or-later',
)

# setup env
#-------------------------------------------------
fs = import('fs')
nix_version = get_option('version')
cpp = meson.get_compiler('cpp')
nix_perl_conf = configuration_data()
nix_perl_conf.set('PACKAGE_VERSION', nix_version)


# set error arguments
#-------------------------------------------------
error_args = [
  '-Wno-pedantic',
  '-Wno-non-virtual-dtor',
  '-Wno-unused-parameter',
  '-Wno-variadic-macros',
  '-Wdeprecated-declarations',
  '-Wno-missing-field-initializers',
  '-Wno-unknown-warning-option',
  '-Wno-unused-variable',
  '-Wno-literal-suffix',
  '-Wno-reserved-user-defined-literal',
  '-Wno-duplicate-decl-specifier',
  '-Wno-pointer-bool-conversion',
]

add_project_arguments(
  cpp.get_supported_arguments(error_args),
  language : 'cpp',
)


# set install directories
#-------------------------------------------------
prefix = get_option('prefix')
libdir = join_paths(prefix, get_option('libdir'))

# Dependencies
#============================================================================

# Required Programs
#-------------------------------------------------
xz = find_program('xz')
xsubpp = find_program('xsubpp')
perl = find_program('perl')
curl = find_program('curl')
yath = find_program('yath', required : false)

# Required Libraries
#-------------------------------------------------
bzip2_dep = dependency('bzip2')
curl_dep = dependency('libcurl')
libsodium_dep = dependency('libsodium')
# nix_util_dep = dependency('nix-util')
nix_store_dep = dependency('nix-store')


# Finding Perl Headers is a pain. as they do not have
# pkgconfig available, are not in a standard location,
# and are installed into a version folder. Use the
# Perl binary to give hints about perl include dir.
#-------------------------------------------------
perl_archname = run_command(
  perl, '-e', 'use Config; print $Config{archname};', check: true).stdout()
perl_version = run_command(
  perl, '-e', 'use Config; print $Config{version};', check: true).stdout()
perl_archlibexp = run_command(
  perl, '-e', 'use Config; print $Config{archlibexp};', check: true).stdout()
perl_site_libdir = run_command(
  perl, '-e', 'use Config; print $Config{installsitearch};', check: true).stdout()
nix_perl_install_dir = join_paths(
  libdir, 'perl5', 'site_perl', perl_version, perl_archname)


# print perl hints for logs
#-------------------------------------------------
message('Perl archname: @0@'.format(perl_archname))
message('Perl version: @0@'.format(perl_version))
message('Perl archlibexp: @0@'.format(perl_archlibexp))
message('Perl install site: @0@'.format(perl_site_libdir))
message('Assumed Nix-Perl install dir: @0@'.format(nix_perl_install_dir))

# Now find perl modules
#-------------------------------------------------
perl_check_dbi = run_command(
  perl,
  '-e', 'use DBI; use DBD::SQLite;',
  '-I@0@'.format(get_option('dbi_path')),
  '-I@0@'.format(get_option('dbd_sqlite_path')),
  check: true
)

if perl_check_dbi.returncode() == 2
  error('The Perl modules DBI and/or DBD::SQLite are missing.')
else
  message('Found Perl Modules: DBI, DBD::SQLite.')
endif



# declare perl dependency
#-------------------------------------------------
perl_dep = declare_dependency(
  dependencies : cpp.find_library(
  'perl',
  has_headers : [
    join_paths(perl_archlibexp, 'CORE', 'perl.h'),
    join_paths(perl_archlibexp, 'CORE', 'EXTERN.h')],
    dirs : [
      join_paths(perl_archlibexp, 'CORE'),
    ],
  ),
  include_directories : join_paths(perl_archlibexp, 'CORE'),
)

# declare dependencies
#-------------------------------------------------
nix_perl_store_dep_list = [
  perl_dep,
  bzip2_dep,
  curl_dep,
  libsodium_dep,
  nix_store_dep,
]

# # build
# #-------------------------------------------------
subdir('lib/Nix')

if get_option('tests').enabled()
  subdir('t')
  test(
    'nix-perl-test',
    yath,
    args : [
      '-I=@0@'.format( join_paths( meson.current_build_dir(), 'lib', 'Nix') ),
    ],
  )
endif
