include(ExternalProject)

set(TOML11_LANGSPEC_GIT_REPOSITORY "https://github.com/toml-lang/toml" CACHE STRING
    "URL of the TOML language specification repository")

set(TOML11_LANGSPEC_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/toml" CACHE FILEPATH
    "directory for the TOML language specification tree")

if(NOT EXISTS "${TOML11_LANGSPEC_SOURCE_DIR}/toml.abnf")
    ExternalProject_Add(toml
        SOURCE_DIR "${TOML11_LANGSPEC_SOURCE_DIR}"
        GIT_REPOSITORY "${TOML11_LANGSPEC_GIT_REPOSITORY}"
        GIT_TAG "v0.5.0"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND "")
endif()

set(TEST_NAMES
    test_datetime
    test_string
    test_utility
    test_result
    test_traits
    test_value
    test_lex_boolean
    test_lex_integer
    test_lex_floating
    test_lex_datetime
    test_lex_string
    test_lex_key_comment
    test_parse_boolean
    test_parse_integer
    test_parse_floating
    test_parse_string
    test_parse_datetime
    test_parse_array
    test_parse_table
    test_parse_inline_table
    test_parse_key
    test_parse_table_key
    test_literals
    test_comments
    test_get
    test_get_or
    test_find
    test_find_or
    test_find_or_recursive
    test_expect
    test_parse_file
    test_serialize_file
    test_parse_unicode
    test_error_detection
    test_format_error
    test_extended_conversions
)

CHECK_CXX_COMPILER_FLAG("-Wall"      COMPILER_SUPPORTS_WALL)
CHECK_CXX_COMPILER_FLAG("-Wextra"    COMPILER_SUPPORTS_WEXTRA)
CHECK_CXX_COMPILER_FLAG("-Wpedantic" COMPILER_SUPPORTS_WPEDANTIC)
CHECK_CXX_COMPILER_FLAG("-Werror"    COMPILER_SUPPORTS_WERROR)

CHECK_CXX_COMPILER_FLAG("-Wsign-conversion"     COMPILER_SUPPORTS_WSIGN_CONVERSION)
CHECK_CXX_COMPILER_FLAG("-Wconversion"          COMPILER_SUPPORTS_WCONVERSION)
CHECK_CXX_COMPILER_FLAG("-Wduplicated-cond"     COMPILER_SUPPORTS_WDUPLICATED_COND)
CHECK_CXX_COMPILER_FLAG("-Wduplicated-branches" COMPILER_SUPPORTS_WDUPLICATED_BRANCHES)
CHECK_CXX_COMPILER_FLAG("-Wlogical-op"          COMPILER_SUPPORTS_WLOGICAL_OP)
CHECK_CXX_COMPILER_FLAG("-Wuseless-cast"        COMPILER_SUPPORTS_WUSELESS_CAST)
CHECK_CXX_COMPILER_FLAG("-Wdouble-promotion"    COMPILER_SUPPORTS_WDOUBLE_PROMOTION)
CHECK_CXX_COMPILER_FLAG("-Wrange-loop-analysis" COMPILER_SUPPORTS_WRANGE_LOOP_ANALYSIS)
CHECK_CXX_COMPILER_FLAG("-Wundef"               COMPILER_SUPPORTS_WUNDEF)
CHECK_CXX_COMPILER_FLAG("-Wshadow"              COMPILER_SUPPORTS_WSHADOW)

include(CheckCXXSourceCompiles)

# check which standard library implementation is used. If libstdc++ is used,
# it will fail to compile. It compiles if libc++ is used.
check_cxx_source_compiles("
#include <cstddef>
#ifdef __GLIBCXX__
    static_assert(false);
#endif
int main() {
    return 0;
}" TOML11_WITH_LIBCXX_LIBRARY)

# LLVM 8 requires -lc++fs if compiled with libc++ to use <filesystem>.
# LLVM 9+ does not require any special library.
# GCC 8 requires -lstdc++fs. GCC 9+ does not require it.
#
# Yes, we can check the version of the compiler used in the current build
# directly in CMake. But, in most cases, clang build uses libstdc++ as the
# standard library implementation and it makes the condition complicated.
# In many environment, the default installed C++ compiler is GCC and libstdc++
# is installed along with it. In most build on such an environment, even if we
# chose clang as the C++ compiler, still libstdc++ is used. Checking default
# gcc version makes the condition complicated.
# The purpose of this file is to compile tests. We know the environment on which
# the tests run. We can set this option and, I think, it is easier and better.
option(TOML11_REQUIRE_FILESYSTEM_LIBRARY "need to link -lstdc++fs or -lc++fs" OFF)

find_package(Boost COMPONENTS unit_test_framework REQUIRED)

set(PREVIOUSLY_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
set(PREVIOUSLY_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES}")

list(APPEND CMAKE_REQUIRED_INCLUDES ${Boost_INCLUDE_DIRS})
list(APPEND CMAKE_REQUIRED_LIBRARIES ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY})

check_cxx_source_compiles("
#define BOOST_TEST_MODULE \"dummy\"
#undef BOOST_TEST_DYN_LINK
#define BOOST_TEST_NO_LIB
#include <boost/test/included/unit_test.hpp>
BOOST_AUTO_TEST_CASE(proforma) { BOOST_TEST(true); }
" TOML11_WITH_BOOST_TEST_HEADER)

check_cxx_source_compiles("
#define BOOST_TEST_MODULE \"dummy\"
#undef BOOST_TEST_DYN_LINK
#undef BOOST_TEST_NO_LIB
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_CASE(proforma) { BOOST_TEST(true); }
" TOML11_WITH_BOOST_TEST_STATIC)

check_cxx_source_compiles("
#define BOOST_TEST_MODULE \"dummy\"
#define BOOST_TEST_DYN_LINK
#undef BOOST_TEST_NO_LIB
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_CASE(proforma) { BOOST_TEST(true); }
" TOML11_WITH_BOOST_TEST_DYNAMIC)

set(CMAKE_REQUIRED_INCLUDES "${PREVIOUSLY_REQUIRED_INCLUDES}")
set(CMAKE_REQUIRED_LIBRARIES "${PREVIOUSLY_REQUIRED_LIBRARIES}")

unset(PREVIOUSLY_REQUIRED_INCLUDES)
unset(PREVIOUSLY_REQUIRED_LIBRARIES)

if(TOML11_WITH_BOOST_TEST_DYNAMIC)
    add_definitions(-DUNITTEST_FRAMEWORK_LIBRARY_EXIST -DBOOST_TEST_DYN_LINK)
elseif(TOML11_WITH_BOOST_TEST_STATIC)
    add_definitions(-DUNITTEST_FRAMEWORK_LIBRARY_EXIST)
elseif(TOML11_WITH_BOOST_TEST_HEADER)
    add_definitions(-DBOOST_TEST_NO_LIB)
else()
    message(FATAL_ERROR "Neither the Boost.Test static or shared library nor the header-only version seem to be usable.")
endif()

if(COMPILER_SUPPORTS_WALL)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()
if(COMPILER_SUPPORTS_WEXTRA)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wextra")
endif()
if(COMPILER_SUPPORTS_WPEDANTIC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpedantic")
endif()
if(COMPILER_SUPPORTS_WERROR)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
endif()
if(COMPILER_SUPPORTS_WSHADOW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wshadow")
endif()
if(COMPILER_SUPPORTS_WSIGN_CONVERSION)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-conversion")
endif()
if(COMPILER_SUPPORTS_WCONVERSION)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wconversion")
endif()
if(COMPILER_SUPPORTS_WDUPLICATED_COND)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wduplicated-cond")
endif()
if(COMPILER_SUPPORTS_WDUPLICATED_BRANCHES)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wduplicated-branches")
endif()
if(COMPILER_SUPPORTS_WLOGICAL_OP)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wlogical-op")
endif()
if(COMPILER_SUPPORTS_WUSELESS_CAST)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wuseless-cast")
endif()
if(COMPILER_SUPPORTS_WDOUBLE_PROMOTION)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wdouble-promotion")
endif()
if(COMPILER_SUPPORTS_WRANGE_LOOP_ANALYSIS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wrange-loop-analysis")
endif()
if(COMPILER_SUPPORTS_WUNDEF)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wundef")
endif()

if(TOML11_USE_UNRELEASED_TOML_FEATURES)
    message(STATUS "adding TOML11_USE_UNRELEASED_TOML_FEATURES flag")
    add_definitions("-DTOML11_USE_UNRELEASED_TOML_FEATURES")
endif()

# Disable some MSVC warnings
if(MSVC)
    # conversion from 'double' to 'unsigned int', possible loss of data
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4244")
    # conversion from 'int' to 'unsigned int', signed/unsigned mismatch
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4365")
    # layout of class may have changed from a previous version of the compiler
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4371")
    # enumerator in switch of enum is not explicitly handled by a case label
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4061")
    # unreferenced inline function has been removed
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4514")
    # constructor is not implicitly called
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4582")
    # destructor is not implicitly called
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4583")
    # pragma warning: there is no warning number <x>
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4619")
    # default constructor was implicitly defined as deleted
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4623")
    # copy constructor was implicitly defined as deleted
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4625")
    # assignment operator was implicitly defined as deleted
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4626")
    # move assignment operator was implicitly defined as deleted
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4627")
    # <X> is not defined as a preprocessor macro, replacing with '0' for '#if/#elif'
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4668")
    # function not inlined
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4710")
    # function selected for automatic inlining
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4711")
    # <x> bytes padding added after data member
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4820")
    # pragma warning(pop): likely mismatch, popping warning state pushed in different file
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd5031")
	# pragma warning(pop): spectre warnings in tests
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd5045")
	# pragma warning(pop): spectre warnings in tests
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4265")
endif()

set(TEST_ENVIRON "TOMLDIR=${TOML11_LANGSPEC_SOURCE_DIR}")
if(WIN32)
    # Set the PATH to be able to find Boost DLL
    STRING(REPLACE ";" "\\;" PATH_STRING "$ENV{PATH}")
    list(APPEND TEST_ENVIRON "PATH=${PATH_STRING}\;${Boost_LIBRARY_DIRS}")
endif()

foreach(TEST_NAME ${TEST_NAMES})
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME} ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY} toml11::toml11)
    target_include_directories(${TEST_NAME} SYSTEM PRIVATE ${Boost_INCLUDE_DIRS})
    target_compile_definitions(${TEST_NAME} PRIVATE "BOOST_TEST_MODULE=\"${TEST_NAME}\"")

    # to compile tests with <filesystem>...
    if(TOML11_REQUIRE_FILESYSTEM_LIBRARY)
        if(TOML11_WITH_LIBCXX_LIBRARY)
            target_link_libraries(${TEST_NAME} "c++fs")
        else()
            target_link_libraries(${TEST_NAME} "stdc++fs")
        endif()
    endif()

    target_include_directories(${TEST_NAME} PRIVATE ${Boost_INCLUDE_DIRS})

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        if(toml11_TEST_WITH_ASAN)
            set_target_properties(${TEST_NAME} PROPERTIES
                COMPILE_FLAGS "-fsanitize=address -fno-omit-frame-pointer"
                LINK_FLAGS    "-fsanitize=address -fno-omit-frame-pointer")
        elseif(toml11_TEST_WITH_UBSAN)
            set_target_properties(${TEST_NAME} PROPERTIES
                COMPILE_FLAGS "-fsanitize=undefined"
                LINK_FLAGS    "-fsanitize=undefined")
        endif()
    endif()

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(${TEST_NAME} PROPERTIES ENVIRONMENT "${TEST_ENVIRON}")
endforeach(TEST_NAME)


# this test is to check it compiles. it will not run
add_executable(test_multiple_translation_unit
               test_multiple_translation_unit_1.cpp
               test_multiple_translation_unit_2.cpp)
target_link_libraries(test_multiple_translation_unit toml11::toml11)

if(WIN32)
    add_executable(test_windows test_windows.cpp)
    target_link_libraries(test_windows toml11::toml11)
endif()

