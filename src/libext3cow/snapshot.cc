
/* snapshot.c - an ext3cow tool for taking file system snapshots
 * Copyright (C) 2003-2007 Zachary N. J. Peterson
 */


#include "ext3cow_tools.h"
#include "snapshot.hh"

//using namespace nix;

namespace nix {

/*
void snapshot_usage(void){
  
  fprintf(stderr, "usage: snapshot <mountpoint>\n");

}


int snapshot_main(int argc, char** argv){

  int fd;
  int ret;
  unsigned int epoch = 0;
  char path[256] = ".\0";
  
  if(argc > 1)
    strcpy(path, argv[1]);

  printf("Snapshot on %s: ", path);

  fd = open(path, O_RDONLY);
  
  // test for ext3cow fs type

  if(fd < 0){
    printf("failed.\n");
    perror(argv[0]);
    exit(1);
  }

  epoch = (int)ioctl(fd, EXT3COW_IOC_TAKESNAPSHOT, &epoch);
  if((int)epoch < 0){
    printf("failed.\n");
    perror(argv[0]);
    exit(1);
  }

  printf("%u\n", (unsigned int)epoch);


  return 0;

}
*/

//End original function
unsigned int take_snapshot(const char* dir) //const string & file_or_dir)
{
  //const char* dir = "test";

  int fd;
  int ret;
  unsigned int epoch = 0;
  
  //char path[256] = ".\0";
  //TODO 256 length check ???
  //strcpy(path, dir);

  fd = open(dir, O_RDONLY);
  
  /* test for ext3cow fs type */

  if(fd < 0){
    printf("failed.\n");
    perror(dir);
    exit(1);
  }

  epoch = (int)ioctl(fd, EXT3COW_IOC_TAKESNAPSHOT, &epoch);
  if((int)epoch < 0){
    printf("failed.\n");
    perror(dir);
    exit(1);
  }
  
  printf("%u\n", (unsigned int)epoch);

  return epoch;
}

}