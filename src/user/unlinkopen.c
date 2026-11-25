// SPDX-License-Identifier: BSD-2-Clause
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  char *path = "/mnt/uop.txt";
  int wfd, rfd, nfd, n;
  char buf[32];

  if(argc > 1)
    path = argv[1];

  unlink(path);

  if((wfd = open(path, O_WRONLY | O_CREATE)) < 0){
    printf("unlinkopen: FAIL open-for-write %s\n", path);
    exit(1);
  }
  if(write(wfd, "hello", 5) != 5){
    printf("unlinkopen: FAIL write\n");
    exit(1);
  }

  if((rfd = open(path, O_RDONLY)) < 0){
    printf("unlinkopen: FAIL open-for-read %s\n", path);
    exit(1);
  }

  if(unlink(path) < 0){
    printf("unlinkopen: FAIL unlink %s\n", path);
    exit(1);
  }

  n = read(rfd, buf, 5);
  if(n != 5 || memcmp(buf, "hello", 5) != 0){
    printf("unlinkopen: FAIL (read %d bytes after unlink)\n", n);
    exit(1);
  }

  // Recreate a new file at the same path (reusing the freed directory
  // slot on FAT32) while extending the unlinked open FD. Writes to the
  // anonymous FD must remain visible to rfd without corrupting the new
  // directory entry (BUG-FS-04 regression).
  if((nfd = open(path, O_WRONLY | O_CREATE | O_TRUNC)) < 0){
    printf("unlinkopen: FAIL recreate %s\n", path);
    exit(1);
  }
  if(write(nfd, "NEW_FILE_DATA", 13) != 13){
    printf("unlinkopen: FAIL write recreated file\n");
    close(nfd);
    exit(1);
  }
  close(nfd);

  if(write(wfd, "world!", 6) != 6){
    printf("unlinkopen: FAIL write unlinked fd\n");
    exit(1);
  }
  n = read(rfd, buf, 6);
  if(n != 6 || memcmp(buf, "world!", 6) != 0){
    printf("unlinkopen: FAIL (read %d extended bytes after unlink)\n", n);
    exit(1);
  }

  close(wfd);
  close(rfd);

  if((nfd = open(path, O_RDONLY)) < 0){
    printf("unlinkopen: FAIL reopen recreated %s\n", path);
    exit(1);
  }
  n = read(nfd, buf, sizeof(buf));
  close(nfd);
  if(n != 13 || memcmp(buf, "NEW_FILE_DATA", 13) != 0){
    printf("unlinkopen: FAIL recreated file corrupted (n=%d)\n", n);
    exit(1);
  }
  if(unlink(path) < 0){
    printf("unlinkopen: FAIL final unlink %s\n", path);
    exit(1);
  }

  if(open(path, O_RDONLY) >= 0){
    printf("unlinkopen: FAIL (file resurrected after close)\n");
    exit(1);
  }

  printf("unlinkopen: ok\n");
  exit(0);
}
