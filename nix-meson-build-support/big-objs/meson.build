if host_machine.system() == 'windows'
  # libexpr's primops creates a large object
  # Without the following flag, we'll get errors when cross-compiling to mingw32:
  # Fatal error: can't write 66 bytes to section .text of src/libexpr/libnixexpr.dll.p/primops.cc.obj: 'file too big'
  add_project_arguments([ '-Wa,-mbig-obj' ], language : 'cpp')
endif
