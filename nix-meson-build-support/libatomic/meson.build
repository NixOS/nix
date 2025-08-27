
# Check if -latomic is needed
# This is needed for std::atomic on some platforms
# We did not manage to test this reliably on all platforms, so we hardcode
# it for now.
if host_machine.cpu_family() in [ 'arm', 'ppc' ]
  deps_other += cxx.find_library('atomic')
endif
