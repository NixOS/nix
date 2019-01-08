libutil = library(
  'libnixutil',
  [
    'affinity.cc',
    'archive.cc',
    'args.cc',
    'compression.cc',
    'config.cc',
    'hash.cc',
    'json.cc',
    'logging.cc',
    'serialise.cc',
    'thread-pool.cc',
    'util.cc',
    'xml-writer.cc',
  ],
  install : true,
  include_directories : global_subdirs,
  dependencies : [
    global_libs,

    liblzma,
    bz2,
    pthread,
    openssl,
    libbrotli,
    boost_context,
  ],
)
