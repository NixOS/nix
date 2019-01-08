libmain = library(
  'libnixmain',
  [
    'common-args.cc',
    'shared.cc',
    'stack.cc',
  ],
  install : true,
  include_directories : global_subdirs,
  dependencies : [
    global_libs,

    openssl,
  ],
  link_with : [
    libstore,
    libutil,
  ],
)
