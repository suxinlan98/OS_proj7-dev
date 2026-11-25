// SPDX-License-Identifier: BSD-2-Clause
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define TOTAL (1024 * 1024)
#define CHUNK 512

static uchar
pattern(int i)
{
  return (i * 7 + 13) & 0xFF;
}

static int
write_file(char *path)
{
  uchar buf[CHUNK];
  int fd, i, n, off;

  if((fd = open(path, O_WRONLY | O_CREATE | O_TRUNC)) < 0){
    printf("bigfile: FAIL open-for-write %s failed\n", path);
    return -1;
  }
  for(off = 0; off < TOTAL; off += n){
    n = TOTAL - off < CHUNK ? TOTAL - off : CHUNK;
    for(i = 0; i < n; i++)
      buf[i] = pattern(off + i);
    if(write(fd, buf, n) != n){
      printf("bigfile: FAIL write failed at %d\n", off);
      close(fd);
      return -1;
    }
  }
  close(fd);
  return 0;
}

static int
verify_file(char *path)
{
  uchar buf[CHUNK];
  int fd, i, n, off;

  if((fd = open(path, O_RDONLY)) < 0){
    printf("bigfile: FAIL open-for-read %s failed\n", path);
    return -1;
  }
  for(off = 0; off < TOTAL; off += n){
    n = read(fd, buf, CHUNK);
    if(n <= 0){
      printf("bigfile: FAIL short read at %d (got %d)\n", off, n);
      close(fd);
      return -1;
    }
    for(i = 0; i < n; i++){
      if(buf[i] != pattern(off + i)){
        printf("bigfile: FAIL mismatch at byte %d\n", off + i);
        close(fd);
        return -1;
      }
    }
  }
  if(read(fd, buf, 1) != 0){
    printf("bigfile: FAIL trailing data past %d bytes\n", TOTAL);
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

static int
run_single(char *path, char *mode)
{
  if(strcmp(mode, "full") != 0 &&
     strcmp(mode, "keep") != 0 &&
     strcmp(mode, "verify") != 0){
    printf("bigfile: FAIL unknown mode %s\n", mode);
    return -1;
  }
  if(strcmp(mode, "verify") != 0){
    if(write_file(path) < 0)
      return -1;
  }
  if(verify_file(path) < 0)
    return -1;
  if(strcmp(mode, "full") == 0){
    if(unlink(path) < 0){
      printf("bigfile: FAIL unlink %s failed\n", path);
      return -1;
    }
  }
  printf("bigfile: ok (%d bytes, %s)\n", TOTAL, mode);
  return 0;
}

int
main(int argc, char *argv[])
{
  char *path = "/mnt/bigfile";
  char *mode = "full";

  if(argc > 1)
    path = argv[1];
  if(argc > 2)
    mode = argv[2];

  if(strcmp(mode, "parallel") == 0){
    int nkids = argc - 3;
    int i, fail = 0;
    if(nkids <= 0){
      printf("bigfile: FAIL parallel requires worker paths\n");
      exit(1);
    }
    for(i = 0; i < nkids; i++){
      int pid = fork();
      if(pid < 0){
        printf("bigfile: FAIL fork failed\n");
        fail = 1;
        nkids = i;
        break;
      }
      if(pid == 0){
        exit(run_single(argv[3 + i], "keep") < 0 ? 1 : 0);
      }
    }
    if(run_single(path, "full") < 0)
      fail = 1;
    for(i = 0; i < nkids; i++){
      int st = -1;
      if(wait(&st) < 0 || st != 0)
        fail = 1;
    }
    if(fail)
      exit(1);
    exit(0);
  }

  if(run_single(path, mode) < 0)
    exit(1);
  exit(0);
}
