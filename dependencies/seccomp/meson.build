# OS Specific checks 
#============================================================================


# Look for libsecppomp, required for Linux sandboxing
#----------------------------------------------------
if sys_name == 'linux'
    libseccomp_dep = dependency(
        'libseccomp',
        version : '>= 2.3.1',
        not_found_message : 'Nix requires libseccomp on a linux host system')
else
    libseccomp_dep = dependency('', required: false)
endif


# set config variable(s)
#--------------------------------------------------
config_h.set(
    'HAVE_SECCOMP', 
    libseccomp_dep.found().to_int(),
    description : 'Define if the libseccomp is available.')
