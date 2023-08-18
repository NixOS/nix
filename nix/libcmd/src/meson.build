# libcmd build file
#============================================================================

libcmd_inc = [include_directories('.')]

# dependencies
#============================================================================

libcmd_dep_list = [
    gc_dep,
    editline_dep,
    libsodium_dep,
    liblowdown_dep,
    nlohmann_dep,
    openssl_dep,
    pthread_dep,
    
    libutil_dep,
    libmain_dep,
    libstore_dep,
    libexpr_dep,
    libfetchers_dep]


# src files
#============================================================================

libcmd_src = files(
    'command.cc',
    'common-eval-args.cc',
    'editor-for.cc',

    'installable-attr-path.cc',
    'installable-derived-path.cc',
    'installable-flake.cc',
    'installables.cc',
    'legacy.cc',
    'markdown.cc',
    'repl.cc')

libcmd_headers = files(
    'command.hh',
    'common-eval-args.hh',
    'editor-for.hh',
    'installable-attr-path.hh',
    'installable-derived-path.hh',
    'installable-flake.hh',
    'installables.hh',
    'legacy.hh',
    'markdown.hh')
