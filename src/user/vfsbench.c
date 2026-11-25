#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define BUFSIZE 512
#define TOTAL (1024 * 1024)

static int total = TOTAL;

int
main(int argc, char *argv[])
{
  char *path = "/vfsbench.tmp";
  int iters = 3;
  char buf[BUFSIZE];
  int fd, i, j, off, n, got, t0, t1;

  if(argc > 1)
    path = argv[1];
  if(argc > 2)
    iters = atoi(argv[2]);
  if(argc > 3){
    int kb = atoi(argv[3]);
    if(kb <= 0 || kb > 0x7fffffff / 1024){
      printf("vfsbench: FAIL invalid size %s\n", argv[3]);
      exit(1);
    }
    total = kb * 1024;
  }
  if(iters <= 0){
    printf("vfsbench: FAIL invalid iters\n");
    exit(1);
  }

  memset(buf, 'x', sizeof(buf));

  t0 = uptime();
  for(i = 0; i < iters; i++){
    if((fd = open(path, O_WRONLY | O_CREATE | O_TRUNC)) < 0){
      printf("vfsbench: FAIL open-for-write %s\n", path);
      exit(1);
    }
    for(off = 0; off < total; off += BUFSIZE){
      if(write(fd, buf, BUFSIZE) != BUFSIZE){
        printf("vfsbench: FAIL write at %d\n", off);
        close(fd);
        exit(1);
      }
    }
    close(fd);
    if(unlink(path) < 0){
      printf("vfsbench: FAIL unlink %s\n", path);
      exit(1);
    }
  }
  t1 = uptime();
  printf("vfsbench: write %d x %d bytes = %d ticks\n", iters, total, t1 - t0);

  if((fd = open(path, O_WRONLY | O_CREATE | O_TRUNC)) < 0){
    printf("vfsbench: FAIL prepare %s\n", path);
    exit(1);
  }
  for(off = 0; off < total; off += BUFSIZE){
    if(write(fd, buf, BUFSIZE) != BUFSIZE){
      printf("vfsbench: FAIL prepare write at %d\n", off);
      close(fd);
      exit(1);
    }
  }
  close(fd);

  t0 = uptime();
  for(i = 0; i < iters; i++){
    if((fd = open(path, O_RDONLY)) < 0){
      printf("vfsbench: FAIL open-for-read %s\n", path);
      exit(1);
    }
    got = 0;
    while((n = read(fd, buf, BUFSIZE)) > 0){
      for(j = 0; j < n; j++){
        if(buf[j] != 'x'){
          printf("vfsbench: FAIL data mismatch at %d\n", got + j);
          close(fd);
          exit(1);
        }
      }
      got += n;
    }
    if(n < 0 || got != total){
      printf("vfsbench: FAIL read got %d expected %d (rc=%d)\n", got, total, n);
      close(fd);
      exit(1);
    }
    close(fd);
  }
  t1 = uptime();
  printf("vfsbench: read %d x %d bytes = %d ticks\n", iters, total, t1 - t0);

  if(unlink(path) < 0){
    printf("vfsbench: FAIL final unlink %s\n", path);
    exit(1);
  }
  exit(0);
}
