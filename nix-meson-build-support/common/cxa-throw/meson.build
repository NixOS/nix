have_cxa_throw = false

if host_machine.system() == 'linux'
  have_cxa_throw = true

  if get_option('default_library') == 'static'
    wrap_cxa_throw_args = [ '-Wl,--wrap=__cxa_throw' ]

    wrap_cxa_throw_lib = static_library(
      'wrap-cxa-throw',
      'wrap-cxa-throw.cc',
    )

    cxa_throw_dep = declare_dependency(
      link_whole : wrap_cxa_throw_lib,
      link_args : wrap_cxa_throw_args + [ '-Wl,-u,__wrap___cxa_throw' ],
    )

  else
    interpose_cxa_throw_lib = static_library(
      'interpose-cxa-throw',
      'interpose-cxa-throw.cc',
      dependencies : cxx.find_library('dl', required : false),
    )

    cxa_throw_dep = declare_dependency(
      link_whole : interpose_cxa_throw_lib,
    )
  endif
endif
