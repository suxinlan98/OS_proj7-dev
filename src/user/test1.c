#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define NULL 0
#define LEN 1024

int test_copy_file(const char *src, const char *dst) {
  int inf, outf;

  if (src == NULL || dst == NULL) {
    fprintf(2, "%s: illegal parameters\n", __func__);
    return 1;
  }
  
  if ((inf = open(src, O_RDONLY)) < 0) {
    fprintf(2, "%s: open %s error\n", __func__, src);
    return 2;
  }
  
  if ((outf = open(dst, O_WRONLY | O_CREATE | O_TRUNC)) < 0) {
    fprintf(2, "%s: open %s error\n", __func__, dst);
    close(inf);
    return 3;
  }

  int i;
  char buf[LEN];
  while ((i = read(inf, buf, LEN)) > 0) {
    if (write(outf, buf, i) != i) {
      fprintf(2, "test1: error write\n");
      close(inf);
      close(outf);
      return 4;
    }
  }
  if (i < 0) {
    fprintf(2, "test1: error read\n");
    close(inf);
    close(outf);
    return 5;
  }
  close(inf);
  close(outf);

  printf("%s(%s,%s): succeed\n", __func__, src, dst);
  return 0;
}


static const char * fnames[][2] = {
  {"/mnt/hello.c",    "/hello.c"},
  {"/mnt/hello.c",    "/tests/hello.c"},
  {"/hello.c",        "/mnt/hello.c"},
  {"/tests/hello.c",  "/mnt/hello.c"}
};

#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
#define NUM_TESTS NELEM(fnames)

int main(int argc, char *argv[]) {
  int fail = 0;
  printf("\n--------- %s: begin... -----------------{\n", argv[0]);
  for (int i=0; i<NUM_TESTS; i++) {
    if (test_copy_file(fnames[i][0], fnames[i][1]) != 0)
      fail = 1;
  }
  printf("----------- %s: finished -----------------}\n", argv[0]);
  return fail;
}
