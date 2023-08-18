# Editline Dependency File
#============================================================================


# Look for editline, a required dependency
#--------------------------------------------------
# NOTE: The the libeditline.pc file was added only in libeditline >= 1.15.2, see 
# https://github.com/troglobit/editline/commit/0a8f2ef4203c3a4a4726b9dd1336869cd0da8607,
# but e.g. Ubuntu 16.04 has an older version, so we fall back to searching for
# editline.h when the pkg-config approach fails.

editline_dep = cpp.find_library('editline')

if not (
    cpp.has_header(
        'editline.h',
        dependencies : editline_dep))
    error('Nix requires editline.h; however the header was not found.')
endif


if not (
    cpp.has_function(
        'read_history',
        prefix : '#include <stdio.h>\n#include "editline.h"',
        dependencies : editline_dep))
    error('Nix requires libeditline; However, required functions do not work.' +\
        'Maybe libeditline version is too old? >= 1.14 is required.')
endif

# set config variable(s)
#--------------------------------------------------
config_h.set(
    'HAVE_EDITLINE_H', 1,
    description : 'Define to 1 if you have the <editline.h> header file.')
