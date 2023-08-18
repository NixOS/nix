# Boost Dependency File
#============================================================================


# Look for boost, a required dependency
#----------------------------------------------------
boost_mod_list = [
  'system',
  'context',
  'thread']


boost_dep = dependency(
  'boost',
  modules: boost_mod_list)


# set config variable(s)
#--------------------------------------------------
config_h.set(
    'HAVE_BOOST', 1,
    description : 'Define if the Boost library is available.')
