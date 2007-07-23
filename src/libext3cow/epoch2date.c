
/* epoch2date.c - an ext3cow tool for turning epoch numbers into dates.
 * Copyright (C) 2003-2007 Zachary N. J. Peterson
 */


#include "ext3cow_tools.h"

int main(int argc, char** argv){

  time_t time;

  if(argc < 2){
    fprintf(stderr, "usage: %s seconds\n", argv[0]);
    exit(1);
  }

  time = atoi(argv[1]);

  printf("%s", ctime(&time));

  return 0;

}
