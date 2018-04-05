#ifndef MBUF_UTIL
#define MBUF_UTIL

#define mbuf_append_str(mbuf, s) mbuf_append(mbuf, s, strlen(s))

int mbuf_append_fmt(struct mbuf *mbuf, const char *fmt, ...);

#endif /* MBUF_UTIL */