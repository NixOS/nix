# Nix project build file
#============================================================================


# init
#============================================================================


# init project
#-------------------------------------------------
project(
    'nix',
    'cpp',
    meson_version : '>= 0.61.0',
    default_options : [
        'cpp_std=c++20',
        'warning_level=3',
        'optimization=3',
        'debug=true'
    ],
    version : files('.version'),
    license : 'MIT')


# init compiler 
#-------------------------------------------------
cpp = meson.get_compiler('cpp')

add_project_arguments(get_option('cxxflags'), language : 'cpp')
add_project_link_arguments(get_option('ldflags'), language: 'cpp')

if(get_option('optimization') == '0')
  add_project_arguments('-U_FORTIFY_SOURCE',language:['cpp','c'])
  message('Disabling FORTIFY_SOURCE as optimization is set to 0')
endif


# init configuration
#-------------------------------------------------
config_h = configuration_data()

config_h.set(
    'HAVE_CXX17', 1,
    description : 'Define if the compiler supports basic C++17 syntax')

package_name = meson.project_name()
config_h.set_quoted(
    'PACKAGE_NAME', package_name,
    description : 'Define to the full name of this package.'
    )

package_tarname = meson.project_name()
config_h.set_quoted(
    'PACKAGE_TARNAME', package_tarname,
    description : 'Define to the one symbol short name of this package.')

package_version = meson.project_version()
config_h.set_quoted(
    'PACKAGE_VERSION', package_version,
    description : 'Define to the version of this package.')

package_string = '@0@ @1@'.format(package_name, package_version)
config_h.set_quoted(
    'PACKAGE_STRING', package_string,
    description : 'Define to the full name and version of this package.')

package_url = 'https://nixos.org/nix/'
config_h.set_quoted(
    'PACKAGE_URL', package_url,
    description : 'Define to the home page for this package.')

package_bug_url = 'https://github.com/nixos/nix/issues'
config_h.set_quoted(
    'PACKAGE_BUGREPORT', package_bug_url,
    description : 'Define to the address where bug reports for this package should be sent.')


# env
#============================================================================


# set install directories
#-------------------------------------------------
prefix = get_option('prefix')
libdir = join_paths(prefix, get_option('libdir'))
bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
sysconfdir = join_paths(prefix, get_option('sysconfdir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
mandir = join_paths(prefix, get_option('mandir'))
docdir = join_paths(prefix, get_option('docdir'))
includedir = join_paths(prefix, get_option('includedir'))

# set nix directories
#-------------------------------------------------

# State should be stored in /nix/var, unless the user overrides it explicitly.
if get_option('normal_var')
    localstatedir = '/nix/var'
else
    localstatedir =  join_paths(prefix, get_option('localstatedir'))
endif

nixstoredir = get_option('nixstoredir')

profiledir =  join_paths(sysconfdir, 'profile.d')


# Construct a Nix system name (like "i686-linux").
#-------------------------------------------------
machine_name = host_machine.cpu()
sys_name = host_machine.system().to_lower()

cpu_archs = ['x86_64', 'armv6', 'armv7', '']

foreach cpu : cpu_archs 
    if host_machine.cpu().contains(cpu)
        if cpu.contains('armv')
            machine_name = cpu + '1'
        else
            machine_name = cpu
        endif
        break
    endif
endforeach

system= '"' + machine_name + '-' + sys_name + '"'
message('system name: ' + system)
config_h.set(
    'SYSTEM', system,
    description : 'platform identifier (`cpu-os`)')


# required dependencies
#============================================================================


# look for required programs
#--------------------------------------------------
cat = find_program('cat', required : true)
ln = find_program('ln', required : true)
cp = find_program('cp', required : true)
rm = find_program('rm', required : true)
bash = find_program('bash', required : true)
echo = find_program('echo', required : true)
patch = find_program('patch', required : true)
flex = find_program('flex', required : true)
bison = find_program('bison', required : true)
sed = find_program('sed', required : true)
tar = find_program('tar', required : true)
bzip2 = find_program('bzip2', required : true)
gzip = find_program('gzip', required : true)
xz = find_program('xz', required : true)
dot = find_program('dot', required : false)
lsof = find_program('lsof', required : false)
tr = find_program('tr', required : true)
tr = find_program('jq', required : true)
coreutils = run_command('dirname', cat.full_path()).stdout().strip()

# Import meson modules
#--------------------------------------------------
find_program('cmake', required : false)
cmake = import('cmake')

find_program('pkgconfig', required : false)
pkgconfig = import('pkgconfig')

# Check whether the store optimiser can optimise symlinks.
#-------------------------------------------------
gen_header = '''
ln -s bla tmp_link
if ln tmp_link tmp_link2 2> /dev/null; then
    echo 1
else
    echo 0
fi
'''

run_command('sh', '-c', 'rm tmp_link*') 
can_link_symlink = run_command('sh', '-c', gen_header).stdout().strip()
if can_link_symlink.to_int() == 1
    run_command('sh', '-c', 'rm tmp_link*')
endif 

config_h.set(
    'CAN_LINK_SYMLINK', can_link_symlink,
    description : 'Whether link() works on symlinks')


# OS Specific checks 
#============================================================================


# freebsd requires _GNU_SOURCE flag
#---------------------------------------------------
if sys_name == 'freebsd'
    add_project_arguments('-D_GNU_SOURCE', language : 'cpp')
    config_h.set('_GNU_SOURCE', 1)  
endif


# Builing for Darwin requires XMLlint
#---------------------------------------------------
if sys_name == 'Darwin'
    xmllint = find_program('xmllint', required : true)
endif


# Solaris requires -lsocket -lnsl for network functions 
#---------------------------------------------------
if sys_name == 'sunos'
    add_project_arguments('-lsocket', language : 'cpp')
    add_project_arguments('-lnsl', language : 'cpp')
endif




# build
#============================================================================


proj_inc = [include_directories('.')]

# build nix
#---------------------------------------------------
project_dirs = [
    'dependencies',
    'misc',
    # 'doc',
    'nix',

    # 'scripts',
    # 'misc',
    'tests',
]


foreach dir : project_dirs
    subdir(dir)
endforeach
