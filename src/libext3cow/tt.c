
/* tt.c - an ext3cow tool for retreiving the current file system epoch
 * Copyright (C) 2003-2007 Zachary N. J. Peterson
 */

#include "ext3cow_tools.h"

void tt_usage(void){
  
  fprintf(stderr, "usage: tt <mountpoint>\n");

}

int tt_main(int argc, char** argv){

  int fd;
  int ret;
  unsigned int epoch = 0;
  char path[255] = ".\0";
  
  if(argc > 1)
    strcpy(path, argv[1]);


  fd = open(path, O_RDONLY);
  
  /* test for ext3cow fs type */

  if(fd < 0){
    fprintf(stderr, "Couldn't open %s\n", path);
    exit(1);
  }


  ret = ioctl(fd, EXT3COW_IOC_GETEPOCH, &epoch);
  if(ret < 0){
    printf("tt on %s failed.\n", path);
    perror(argv[0]);
    exit(1);
  }
  printf("Epoch: %d\n", ret);
  close(fd);
  
  return 0;

}
