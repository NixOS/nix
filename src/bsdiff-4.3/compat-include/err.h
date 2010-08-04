/* Simulate BSD's <err.h> functionality. */

#ifndef COMPAT_ERR_H_INCLUDED
#define COMPAT_ERR_H_INCLUDED 1

#include <stdio.h>
#include <stdlib.h>

#define err(rc,...)  do { fprintf(stderr,__VA_ARGS__); exit(rc); } while(0)
#define errx(rc,...) do { fprintf(stderr,__VA_ARGS__); exit(rc); } while(0)

#endif
