# Clang gets grumpy about missing libasan symbols if -shared-libasan is not
# passed when building shared libs, at least on Linux
if cxx.get_id() == 'clang' and ('address' in get_option('b_sanitize') or 'undefined' in get_option(
  'b_sanitize',
))
  add_project_link_arguments('-shared-libasan', language : 'cpp')
endif

if 'address' in get_option('b_sanitize')
  deps_other += declare_dependency(sources : 'asan-options.cc')
endif
