// Demonstrate that moving the "acquire" in iderw after the loop that
// appends to the idequeue results in a race.

// For this to work, you should also add a spin within iderw's
// idequeue traversal loop.  Adding the following demonstrated a panic
// after about 5 runs of stressfs in QEMU on a 2.1GHz CPU:
//    for (i = 0; i < 40000; i++)
//      asm volatile("");

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  int fd, i, j, pid, has_child = 0, st = 0;
  char path[] = "sfs0";
  char data[512];

  printf("stressfs starting\n");
  memset(data, 'a', sizeof(data));

  for(i = 0; i < 4; i++){
    pid = fork();
    if(pid < 0){
      printf("stressfs: FAIL fork\n");
      exit(1);
    }
    if(pid > 0){
      has_child = 1;
      break;
    }
  }

  printf("write %d\n", i);

  path[3] += i;
  fd = open(path, O_CREATE | O_RDWR | O_TRUNC);
  if(fd < 0){
    printf("stressfs: FAIL open write %s\n", path);
    exit(1);
  }
  for(j = 0; j < 20; j++){
    if(write(fd, data, sizeof(data)) != sizeof(data)){
      printf("stressfs: FAIL write %s\n", path);
      close(fd);
      exit(1);
    }
  }
  close(fd);

  printf("read\n");

  fd = open(path, O_RDONLY);
  if(fd < 0){
    printf("stressfs: FAIL open read %s\n", path);
    exit(1);
  }
  for(j = 0; j < 20; j++){
    if(read(fd, data, sizeof(data)) != sizeof(data)){
      printf("stressfs: FAIL read %s\n", path);
      close(fd);
      exit(1);
    }
    for(int k = 0; k < (int)sizeof(data); k++){
      if(data[k] != 'a'){
        printf("stressfs: FAIL data mismatch in %s\n", path);
        close(fd);
        exit(1);
      }
    }
  }
  close(fd);

  if(has_child){
    if(wait(&st) < 0 || st != 0){
      printf("stressfs: FAIL child status %d\n", st);
      exit(1);
    }
  }

  exit(0);
}
