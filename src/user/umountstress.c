// SPDX-License-Identifier: BSD-2-Clause
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define DEFAULT_LOOPS 80

static void
lookup_worker(char *probe, int loops, int pfd)
{
  char buf[32];
  int fd, i, ok = 0;

  for(i = 0; i < loops; i++){
    fd = open(probe, O_RDONLY);
    if(fd >= 0){
      if(read(fd, buf, sizeof(buf)) <= 0){
        close(fd);
        close(pfd);
        exit(1);
      }
      close(fd);
      ok++;
    }
  }
  write(pfd, &ok, sizeof(ok));
  close(pfd);
  exit(0);
}

static void
topology_worker(char *fstype, char *path, int dev, int loops, int pfd)
{
  int i, ok = 0;

  for(i = 0; i < loops; i++){
    if(umount(path) == 0){
      if(mount(fstype, path, dev) < 0){
        close(pfd);
        exit(1);
      }
      ok++;
    }
  }
  write(pfd, &ok, sizeof(ok));
  close(pfd);
  exit(0);
}

int
main(int argc, char *argv[])
{
  char *fstype = "ext2";
  char *path = "/mnt";
  char *probe = "/mnt/hello.c";
  char buf[32];
  int dev = 2;
  int loops = DEFAULT_LOOPS;
  int fd, i, start, status;
  int lp[2], tp[2];
  int total_opens = 0, total_umounts = 0, cnt;

  if(argc != 1 && argc != 6){
    printf("usage: umountstress [fstype path dev probe loops]\n");
    exit(1);
  }
  if(argc == 6){
    fstype = argv[1];
    path = argv[2];
    dev = atoi(argv[3]);
    probe = argv[4];
    loops = atoi(argv[5]);
    if(dev <= 0 || loops <= 0){
      printf("umountstress: FAIL invalid arguments\n");
      exit(1);
    }
  }

  fd = open(probe, O_RDONLY);
  if(fd < 0 || umount(path) == 0){
    printf("umountstress: FAIL open busy\n");
    exit(1);
  }
  close(fd);

  if(pipe(lp) < 0 || pipe(tp) < 0){
    printf("umountstress: FAIL pipe\n");
    exit(1);
  }

  start = uptime();
  for(i = 0; i < 2; i++){
    int pid = fork();
    if(pid < 0){
      printf("umountstress: FAIL fork\n");
      exit(1);
    }
    if(pid == 0){
      close(lp[0]);
      close(tp[0]);
      close(tp[1]);
      lookup_worker(probe, loops, lp[1]);
    }
    pid = fork();
    if(pid < 0){
      printf("umountstress: FAIL fork\n");
      exit(1);
    }
    if(pid == 0){
      close(tp[0]);
      close(lp[0]);
      close(lp[1]);
      topology_worker(fstype, path, dev, loops, tp[1]);
    }
  }
  close(lp[1]);
  close(tp[1]);

  for(i = 0; i < 4; i++){
    if(wait(&status) < 0 || status != 0){
      printf("umountstress: FAIL child\n");
      exit(1);
    }
  }
  for(i = 0; i < 2; i++){
    if(read(lp[0], &cnt, sizeof(cnt)) == sizeof(cnt))
      total_opens += cnt;
    if(read(tp[0], &cnt, sizeof(cnt)) == sizeof(cnt))
      total_umounts += cnt;
  }
  close(lp[0]);
  close(tp[0]);
  if(total_opens <= 0 || total_umounts <= 0){
    printf("umountstress: FAIL zero progress (opens=%d umounts=%d)\n",
           total_opens, total_umounts);
    exit(1);
  }

  fd = open(probe, O_RDONLY);
  if(fd < 0){
    if(mount(fstype, path, dev) < 0)
      goto fail;
    fd = open(probe, O_RDONLY);
  }
  if(fd < 0 || read(fd, buf, sizeof(buf)) <= 0)
    goto fail;
  close(fd);

  printf("UMOUNT_STRESS_OK %s %d loops %d ticks\n",
         fstype, loops, uptime() - start);
  exit(0);

fail:
  if(fd >= 0)
    close(fd);
  printf("umountstress: FAIL final state\n");
  exit(1);
}
