# SQLite Dependency File
#============================================================================


# Look for sqlite3, a required dependency
#--------------------------------------------------
sqlite3_dep = declare_dependency(
    dependencies: cpp.find_library('sqlite3'),
    version : '>= 3.6.19')


config_h.set(
    'HAVE_SQLITE', 1,
    description : 'Define if the SQLite3 is available.')
