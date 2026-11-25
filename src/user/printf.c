#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#include <stdarg.h>

static char digits[] = "0123456789ABCDEF";

struct printbuf {
  int fd;
  int len;
  char data[128];
};

static void
flushbuf(struct printbuf *pb)
{
  if(pb->len > 0){
    write(pb->fd, pb->data, pb->len);
    pb->len = 0;
  }
}

static void
putc(struct printbuf *pb, char c)
{
  pb->data[pb->len++] = c;
  if(pb->len == sizeof(pb->data))
    flushbuf(pb);
}

static void
printint(struct printbuf *pb, long long xx, int base, int sgn)
{
  char buf[20];
  int i, neg;
  uint x;

  neg = 0;
  if(sgn && xx < 0){
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);
  if(neg)
    buf[i++] = '-';

  while(--i >= 0)
    putc(pb, buf[i]);
}

static void
printptr(struct printbuf *pb, uint64 x) {
  int i;
  putc(pb, '0');
  putc(pb, 'x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    putc(pb, digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the given fd. Only understands %d, %x, %p, %c, %s.
void
vprintf(int fd, const char *fmt, va_list ap)
{
  struct printbuf pb;
  char *s;
  int c0, c1, c2, i, state;

  pb.fd = fd;
  pb.len = 0;
  state = 0;
  for(i = 0; fmt[i]; i++){
    c0 = fmt[i] & 0xff;
    if(state == 0){
      if(c0 == '%'){
        state = '%';
      } else {
        putc(&pb, c0);
      }
    } else if(state == '%'){
      c1 = c2 = 0;
      if(c0) c1 = fmt[i+1] & 0xff;
      if(c1) c2 = fmt[i+2] & 0xff;
      if(c0 == 'd'){
        printint(&pb, va_arg(ap, int), 10, 1);
      } else if(c0 == 'l' && c1 == 'd'){
        printint(&pb, va_arg(ap, uint64), 10, 1);
        i += 1;
      } else if(c0 == 'l' && c1 == 'l' && c2 == 'd'){
        printint(&pb, va_arg(ap, uint64), 10, 1);
        i += 2;
      } else if(c0 == 'u'){
        printint(&pb, va_arg(ap, uint32), 10, 0);
      } else if(c0 == 'l' && c1 == 'u'){
        printint(&pb, va_arg(ap, uint64), 10, 0);
        i += 1;
      } else if(c0 == 'l' && c1 == 'l' && c2 == 'u'){
        printint(&pb, va_arg(ap, uint64), 10, 0);
        i += 2;
      } else if(c0 == 'x'){
        printint(&pb, va_arg(ap, uint32), 16, 0);
      } else if(c0 == 'l' && c1 == 'x'){
        printint(&pb, va_arg(ap, uint64), 16, 0);
        i += 1;
      } else if(c0 == 'l' && c1 == 'l' && c2 == 'x'){
        printint(&pb, va_arg(ap, uint64), 16, 0);
        i += 2;
      } else if(c0 == 'p'){
        printptr(&pb, va_arg(ap, uint64));
      } else if(c0 == 'c'){
        putc(&pb, va_arg(ap, uint32));
      } else if(c0 == 's'){
        if((s = va_arg(ap, char*)) == 0)
          s = "(null)";
        for(; *s; s++)
          putc(&pb, *s);
      } else if(c0 == '%'){
        putc(&pb, '%');
      } else {
        // Unknown % sequence.  Print it to draw attention.
        putc(&pb, '%');
        putc(&pb, c0);
      }

      state = 0;
    }
  }
  flushbuf(&pb);
}

void
fprintf(int fd, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(fd, fmt, ap);
  va_end(ap);
}

void
printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(1, fmt, ap);
  va_end(ap);
}
