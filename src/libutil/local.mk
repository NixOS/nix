libraries += libutil

libutil_NAME = libnixutil

libutil_DIR := $(d)

libutil_SOURCES := $(wildcard $(d)/*.cc $(d)/signature/*.cc)
ifdef HOST_UNIX
  libutil_SOURCES += $(wildcard $(d)/unix/*.cc)
endif
ifdef HOST_LINUX
    libutil_SOURCES += $(wildcard $(d)/linux/*.cc)
endif
ifdef HOST_WINDOWS
    libutil_SOURCES += $(wildcard $(d)/windows/*.cc)
endif

# Not just for this library itself, but also for downstream libraries using this library

INCLUDE_libutil := -I $(d)
ifdef HOST_UNIX
  INCLUDE_libutil += -I $(d)/unix
endif
ifdef HOST_LINUX
  INCLUDE_libutil += -I $(d)/linux
endif
ifdef HOST_WINDOWS
  INCLUDE_libutil += -I $(d)/windows
endif
libutil_CXXFLAGS += $(INCLUDE_libutil)

libutil_LDFLAGS += $(THREAD_LDFLAGS) $(LIBCURL_LIBS) $(SODIUM_LIBS) $(OPENSSL_LIBS) $(LIBBROTLI_LIBS) $(LIBARCHIVE_LIBS) $(BOOST_LDFLAGS) -lboost_context

$(foreach i, $(wildcard $(d)/args/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/args, 0644)))
$(foreach i, $(wildcard $(d)/signature/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/signature, 0644)))


ifeq ($(HAVE_LIBCPUID), 1)
  libutil_LDFLAGS += -lcpuid
endif

# BLAKE3 support

# See the comments below for the build rules for the reason we use the `.o` files here.

libutil_SOURCES += \
  $(d)/blake3.o \
  $(d)/blake3_dispatch.o \
  $(d)/blake3_portable.o

ifneq ($(filter aarch64% arm%,$(system)),)
  libutil_SOURCES += $(d)/blake3_neon.o
endif

ifneq ($(filter x86_64%,$(system)),)
  ifneq ($(or $(HOST_UNIX), $(HOST_LINUX)),)
    libutil_SOURCES += \
      $(d)/blake3_sse2_x86-64_unix.o \
      $(d)/blake3_sse41_x86-64_unix.o \
      $(d)/blake3_avx2_x86-64_unix.o \
      $(d)/blake3_avx512_x86-64_unix.o
  endif
  ifdef HOST_WINDOWS
    ifneq ($(or $(HOST_CYGWIN), $(HOST_MINGW)),)
      libutil_SOURCES += \
        $(d)/blake3_sse2_x86-64_windows_gnu.o \
        $(d)/blake3_sse41_x86-64_windows_gnu.o \
        $(d)/blake3_avx2_x86-64_windows_gnu.o \
        $(d)/blake3_avx512_x86-64_windows_gnu.o
    else
      libutil_SOURCES += \
        $(d)/blake3_sse2_x86-64_windows_mscv.o \
        $(d)/blake3_sse41_x86-64_windows_mscv.o \
        $(d)/blake3_avx2_x86-64_windows_mscv.o \
        $(d)/blake3_avx512_x86-64_windows_mscv.o
    endif
  endif
endif

INCLUDE_libutil += -I $(NIX_BLAKE3_SRC)/c

# We use custom rules for the BLAKE3 source files because they are in a read-only store directory
# and the default rules will try write build artifacts there and fail with permission errors.

$(d)/blake3%.o: $(NIX_BLAKE3_SRC)/c/blake3%.S
	$(trace-cc) $(call CC_CMD,$@)

$(d)/blake3%.o: $(NIX_BLAKE3_SRC)/c/blake3%.c
	$(trace-cc) $(call CC_CMD,$@)

$(d)/blake3.o: $(NIX_BLAKE3_SRC)/c/blake3.c
	$(trace-cc) $(call CC_CMD,$@)

$(d)/blake3%.compile_commands.json: $(NIX_BLAKE3_SRC)/c/blake3%.S
	$(trace-jq) $(COMPILE_COMMANDS_JSON_CMD) $(call CC_CMD,$(@:.compile_commands.json=.o)) > $@

$(d)/blake3%.compile_commands.json: $(NIX_BLAKE3_SRC)/c/blake3%.c
	$(trace-jq) $(COMPILE_COMMANDS_JSON_CMD) $(call CC_CMD,$(@:.compile_commands.json=.o)) > $@

$(d)/blake3.compile_commands.json: $(NIX_BLAKE3_SRC)/c/blake3.c
	$(trace-jq) $(COMPILE_COMMANDS_JSON_CMD) $(call CC_CMD,$(@:.compile_commands.json=.o)) > $@
