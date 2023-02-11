# Release X.Y (202?-??-??)

* Commands which take installables on the command line can now read them from the standard input if
  passed the `--stdin` flag. This is primarily useful when you have a large amount of paths which
  exceed the OS arg limit.
