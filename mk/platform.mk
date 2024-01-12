ifdef HOST_OS
  HOST_KERNEL = $(firstword $(subst -, ,$(HOST_OS)))
  ifeq ($(patsubst mingw%,,$(HOST_KERNEL)),)
    HOST_MINGW = 1
    HOST_WINDOWS = 1
  endif
  ifeq ($(HOST_KERNEL), cygwin)
    HOST_CYGWIN = 1
    HOST_WINDOWS = 1
    HOST_UNIX = 1
  endif
  ifeq ($(patsubst darwin%,,$(HOST_KERNEL)),)
    HOST_DARWIN = 1
    HOST_UNIX = 1
  endif
  ifeq ($(patsubst freebsd%,,$(HOST_KERNEL)),)
    HOST_FREEBSD = 1
    HOST_UNIX = 1
  endif
  ifeq ($(patsubst netbsd%,,$(HOST_KERNEL)),)
    HOST_NETBSD = 1
    HOST_UNIX = 1
  endif
  ifeq ($(HOST_KERNEL), linux)
    HOST_LINUX = 1
    HOST_UNIX = 1
  endif
  ifeq ($(patsubst solaris%,,$(HOST_KERNEL)),)
    HOST_SOLARIS = 1
    HOST_UNIX = 1
  endif
endif
