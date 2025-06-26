if host_machine.system() == 'windows'
  # https://learn.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt?view=msvc-170
  # #define _WIN32_WINNT_WIN8 0x0602
  # We currently don't use any API which requires higher than this.
  add_project_arguments([ '-D_WIN32_WINNT=0x0602' ], language : 'cpp')
endif
