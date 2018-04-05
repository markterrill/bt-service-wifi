#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/mbuf.h"

#include "mbuf_util.h"

int mbuf_append_fmt(struct mbuf *mbuf, const char *fmt, ...)
{
  char s[128];
  int i;
  va_list args;

  va_start(args, fmt);
  i = vsnprintf(s, sizeof(s), fmt, args);
  va_end(args);
  mbuf_append(mbuf, s, i);

  return i;
}