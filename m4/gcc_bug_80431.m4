# Ensure that this bug is not present in the C++ toolchain we are using.
#
# URL for bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80431
#
# The test program is from that issue, with only a slight modification
# to set an exit status instead of printing strings.
AC_DEFUN([ENSURE_NO_GCC_BUG_80431],
[
  AC_MSG_CHECKING([that GCC bug 80431 is fixed])
  AC_LANG_PUSH(C++)
  AC_RUN_IFELSE(
    [AC_LANG_PROGRAM(
      [[
        #include <cstdio>

        static bool a = true;
        static bool b = true;

        struct Options { };

        struct Option
        {
            Option(Options * options)
            {
                a = false;
            }

            ~Option()
            {
                b = false;
            }
        };

        struct MyOptions : Options { };

        struct MyOptions2 : virtual MyOptions
        {
            Option foo{this};
        };
      ]],
      [[
        {
            MyOptions2 opts;
        }
        return (a << 1) | b;
      ]])],
    [status_80431=0],
    [status_80431=$?],
    [status_80431=''])
  AC_LANG_POP(C++)
  AS_CASE([$status_80431],
    [''],[
      AC_MSG_RESULT(cannot check because cross compiling)
      AC_MSG_NOTICE(assume we are bug free)
    ],
    [0],[
      AC_MSG_RESULT(yes)
    ],
    [2],[
      AC_MSG_RESULT(no)
      AC_MSG_ERROR(Cannot build Nix with C++ compiler with this bug)
    ],
    [
      AC_MSG_RESULT(unexpected result $status_80431: not expected failure with bug, ignoring)
    ])
])
