cmake_minimum_required(VERSION 3.7)

# The project() command manages VERSION variables.
cmake_policy(SET CMP0048 NEW)

# *Enable* use of the LOCATION property for build targets. (nix_install)
cmake_policy(SET CMP0026 OLD)

project(nix VERSION 1.11.11)
message(STATUS "Project: ${PROJECT_NAME}, version: ${PROJECT_VERSION}")

enable_testing()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Construct a Nix system name (like "i686-linux").
execute_process(OUTPUT_VARIABLE host_cpu COMMAND uname -p)
execute_process(OUTPUT_VARIABLE host_os COMMAND uname -s COMMAND tr "'A-Z '" "'a-z_'" OUTPUT_STRIP_TRAILING_WHITESPACE)
#set(SYSTEM "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}")
set(SYSTEM "${CMAKE_SYSTEM_PROCESSOR}-${host_os}")

# State should be stored in /nix/var, unless the user overrides it explicitly.
set(CMAKE_INSTALL_LOCALSTATEDIR "/nix/var" CACHE PATH "")

# variables are prepended (if necessary) by the value of CMAKE_INSTALL_PREFIX.
include(GNUInstallDirs)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)
include(NixCommon)
include(NixInstall)

include(FindPkgConfig)
include(CheckIncludeFiles)
include(CheckIncludeFileCXX)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckLibraryExists)
#include(CheckPrototypeExists)
include(CheckStructHasMember)

include(CMakePackageConfigHelpers)

# Check for pubsetbuf.
check_cxx_source_compiles("
    #include <iostream>
    using namespace std;
    static char buf[1024];
    int main()
    {
        cerr.rdbuf()->pubsetbuf(buf, sizeof(buf));
    }"
    HAVE_PUBSETBUF
)

check_function_exists(statvfs HAVE_STATVFS)
check_function_exists(pipe2 HAVE_PIPE2)

# Check for lutimes, optionally used for changing the mtime of symlinks.
check_function_exists(lutimes HAVE_LUTIMES)

# Check whether the store optimiser can optimise symlinks.
execute_process(COMMAND ln -s bla tmp_link)
execute_process(COMMAND ! ln tmp_link tmp_link2
    RESULT_VARIABLE retcode)
execute_process(COMMAND rm -f tmp_link tmp_link2)
set(CAN_LINK_SYMLINK "${retcode}" CACHE BOOL "Whether link() works on symlinks.")

# Check for <locale>.
check_include_file_cxx(locale HAVE_LOCALE)

find_program(bash bash)
find_program(patch patch)

find_program(xmllint xmllint)
find_program(xsltproc xsltproc)

find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

find_program(sed sed)
find_program(tar tar)
find_program(bzip2 bzip2)
find_program(gzip gzip)
find_program(xz xz)

find_program(dot dot)
find_program(dblatex dblatex)
find_program(pv pv)

find_program(bro bro)
find_program(lsof lsof)

find_program(cat cat)
find_program(tr tr)

# To populate various *.in files, which require id, stat, ln, mkdir, chmod
#find_package(coreutils REQUIRED)
set(coreutils "/bin/:/usr/bin" CACHE PATH "path of cat, mkdir, etc.")

#option(NIX_STORE_DIR   "path of the Nix store (defaults to /nix/store)"   "/nix/store")
SET(NIX_STORE_DIR "/nix/store" CACHE PATH "path of the Nix store (defaults to /nix/store)")
message(STATUS "NIX_STORE_DIR: ${NIX_STORE_DIR}")

# Look for OpenSSL, a required dependency.
find_package(OpenSSL REQUIRED)
find_program(openssl openssl)

# Look for libbz2, a required dependency.
find_library(LIBBZ2 bz2) # Doesn't ship with a .pc file

# Look for SQLite, a required dependency.
pkg_check_modules(SQLITE3 sqlite3>=3.6.19 REQUIRED)

# Look for libcurl, a required dependency.
find_package(CURL REQUIRED)

# Look for libsodium, an optional dependency.
pkg_check_modules(LIBSODIUM libsodium)
set(HAVE_SODIUM ${LIBSODIUM_FOUND})

# Look for liblzma, a required dependency.
pkg_check_modules(LIBLZMA liblzma REQUIRED)

# Look for libseccomp, required for Linux sandboxing.
pkg_check_modules(LIBSECCOMP libseccomp REQUIRED)

# TODO: Look for aws-cpp-sdk-s3.
# ENABLE_S3

# TODO: Whether to use the Boehm garbage collector.
set(NIX_ENABLE_GC "yes" CACHE BOOL "enable garbage collection in the Nix expression evaluator (requires Boehm GC)")
if(NIX_ENABLE_GC)
    pkg_check_modules(BDW_GC bdw-gc)
    if(BDW_GC_FOUND)
        set(HAVE_BOEHMGC 1)
    endif()
endif()

# TODO?
# option(INIT_STATE, "do not initialise DB etc. in `make install'")

# documentation generation switch
set(NIX_DOC_GENERATE "yes" CACHE BOOL "Documentation generation.")

# Setuid installations.
check_function_exists(lchown HAVE_LCHOWN)

# Nice to have, but not essential.
check_function_exists(strsignal HAVE_STRSIGNAL)
check_function_exists(posix_fallocate HAVE_POSIX_FALLOCATE)
check_function_exists(sysconf HAVE_SYSCONF)

# TODO: This is needed if bzip2 is a static library, and the Nix libraries are dynamic.
# if test "$(uname)" = "Darwin"; then
#     LDFLAGS="-all_load $LDFLAGS"
# fi

# Do we have GNU tar?
execute_process(COMMAND "tar --version 2> /dev/null | grep -q GNU && 
    tar cvf /dev/null --warning=no-timestamp ./config.log > /dev/null"
        RESULT_VARIABLE retcode)
if(NOT "${retcode}" STREQUAL "0")
    set(tarFlags "--warning=no-timestamp")
endif()

set(NIX_SANDBOX_SHELL "/bin/sh" CACHE PATH "path of a statically-linked shell to use as /bin/sh in sandboxes")

check_struct_has_member("struct dirent" d_type dirent.h HAVE_STRUCT_DIRENT_D_TYPE) 

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in
    config.h
    INSTALL_DESTINATION ${CMAKE_BINARY_DIR})

# Equivalent of Makefile-directives here-on-down?
include_directories(${CMAKE_BINARY_DIR})

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/libutil
    ${CMAKE_SOURCE_DIR}/src/libstore
    ${CMAKE_SOURCE_DIR}/src/libmain
    ${CMAKE_SOURCE_DIR}/src/libexpr
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -Wall -include config.h")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wno-unneeded-internal-declaration")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--no-copy-dt-needed-entries")

# Same as: -Wl,-z,defs
# Make it the default (it is already for EXEs)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--no-undefined")

set(storedir        ${NIX_STORE_DIR})
set(localstatedir   ${CMAKE_INSTALL_LOCALSTATEDIR})

set(prefix          ${CMAKE_INSTALL_PREFIX})
set(bindir          ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR})
set(libdir          ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
set(libexecdir      ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBEXECDIR})
set(includedir      ${CMAKE_INSTALL_PREFIX}/include)
set(sysconfdir      ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR})

set(PACKAGE_VERSION ${PROJECT_VERSION})

option(NIX_EXTERNALIZE_DEBUGINFO
    "Generate .debug files and strip executables and libraries" OFF)

add_subdirectory(src/boost/format)
add_subdirectory(src/libutil)
add_subdirectory(src/libstore)
add_subdirectory(src/libmain)
add_subdirectory(src/libexpr)
add_subdirectory(src/nix)
add_subdirectory(src/nix-store)
add_subdirectory(src/nix-instantiate)
add_subdirectory(src/nix-env)
add_subdirectory(src/nix-daemon)
add_subdirectory(src/nix-collect-garbage)
add_subdirectory(src/nix-copy-closure)
add_subdirectory(src/nix-prefetch-url)
add_subdirectory(src/buildenv)
# TODO: Apple only
#add_subdirectory(src/resolve-system-dependencies)
add_subdirectory(src/nix-channel)
add_subdirectory(src/nix-build)
add_subdirectory(src/build-remote)
add_subdirectory(scripts)
add_subdirectory(corepkgs)

add_subdirectory(misc/launchd)
add_subdirectory(misc/emacs)
add_subdirectory(misc/systemd)
add_subdirectory(misc/upstart)

#add_subdirectory(perl)

add_subdirectory(doc/manual)
#add_subdirectory(tests)

set(includedir ${CMAKE_INSTALL_PREFIX}/include)
install(DIRECTORY
    src/libexpr/    # trailing '/' are significant
    src/libmain/
    src/libstore/
    src/libutil/
    DESTINATION ${includedir}/nix
    FILES_MATCHING PATTERN "*.hh"
)
install(FILES 
    ${CMAKE_BINARY_DIR}/config.h 
    DESTINATION ${includedir}/nix
)

install(DIRECTORY src/boost 
    DESTINATION ${includedir}/nix
    FILES_MATCHING PATTERN "*.hpp"
)
