#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define SRC_FILENAME "hello.c"
#define DST_FILENAME "/mnt/hello.c"

int test_cross_fs_copy() {
  printf("将从 xv6 文件系统复制文件到 EXT2 文件系统\n\n");

  int src_fd = open(SRC_FILENAME, O_RDONLY);
  if(src_fd < 0) {
    printf("错误: 找不到源文件 %s\n", SRC_FILENAME);
    return 1;
  }

  int dst_fd = open(DST_FILENAME, O_WRONLY | O_CREATE | O_TRUNC);
  if(dst_fd < 0) {
    printf("错误: 无法创建目标文件 %s\n", DST_FILENAME);
    printf("请先执行 mount\n");
    close(src_fd);
    return 2;
  }

  char copy_buf[512];
  int copy_n;
  while((copy_n = read(src_fd, copy_buf, sizeof(copy_buf))) > 0) {
    if(write(dst_fd, copy_buf, copy_n) != copy_n) {
      printf("✗ 写入失败\n");
      close(src_fd);
      close(dst_fd);
      return 3;
    }
  }

  close(src_fd);
  close(dst_fd);

  if(copy_n < 0) {
    printf("✗ 读取失败\n");
    return 4;
  }

  printf("✓ 文件复制成功！\n\n");

  printf("验证复制结果:\n");
  src_fd = open(SRC_FILENAME, O_RDONLY);
  int fd = open(DST_FILENAME, O_RDONLY);
  if(src_fd < 0 || fd < 0) {
    printf("test2: error cannot open files for verify\n");
    if(src_fd >= 0) close(src_fd);
    if(fd >= 0) close(fd);
    return 5;
  }

  static char verify_buf[256];
  char src_cmp[256], dst_cmp[256];
  int sn, dn, shown = 0;
  verify_buf[0] = '\0';
  for(;;) {
    sn = read(src_fd, src_cmp, sizeof(src_cmp));
    dn = read(fd, dst_cmp, sizeof(dst_cmp));
    if(sn < 0 || dn < 0 || sn != dn || (sn > 0 && memcmp(src_cmp, dst_cmp, sn) != 0)) {
      printf("test2: error content mismatch\n");
      close(src_fd);
      close(fd);
      return 6;
    }
    if(sn == 0)
      break;
    if(!shown) {
      int k = sn < (int)sizeof(verify_buf) - 1 ? sn : (int)sizeof(verify_buf) - 1;
      memmove(verify_buf, dst_cmp, k);
      verify_buf[k] = '\0';
      shown = 1;
    }
  }
  close(src_fd);
  close(fd);

  printf("✓ 复制成功！文件内容: \n%s\n", verify_buf);
  printf("现在您可以用以下命令查看复制的文件:\n");
  printf("cat %s\n", DST_FILENAME);

  printf("%s(): succeed\n", __func__);
  return 0;
}

int main(int argc, char *argv[]) {
  int rc;
  printf("\n--------- %s: begin... ---------------------{\n", argv[0]);
  rc = test_cross_fs_copy();
  printf("--------- %s: finished -------------------}\n", argv[0]);
  return rc;
}
