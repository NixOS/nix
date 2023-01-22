# Release X.Y (202?-??-??)

* A new function `builtins.readFileType` is available. It is similar to
  `builtins.readDir` but acts on a single file or directory.

* The `builtins.readDir` function has been optimized when encountering unknown
  file types from POSIX's `readdir`. In such cases the type of each file is/was
  discovered by making multiple syscalls. This change makes these operations
  lazy such that these lookups will only be performed if the attribute is used.
  This optimization effects a minority of filesystems and operating systems.
