#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utf8.h"

/*
 * Return length of utf8 sequence or 0 if it's not the first byte of an
 * utf8 sequence.
 */
int utf8_enc_len(int first_char)
{
  if(first_char < 0x80) return 1;
  if(first_char < 0xc0) return 0;
  if(first_char < 0xe0) return 2;
  if(first_char < 0xf0) return 3;
  if(first_char < 0xf8) return 4;
  if(first_char < 0xfc) return 5;
  if(first_char < 0xfe) return 6;

  return 0;
}


/*
 * Decode utf8 string.
 */
int utf8_decode(unsigned char *str)
{
  int len, ucs4 = 0;
  static int mask[6] = { 0x7f, 0x1f, 0xf, 7, 3, 1 };

  len = utf8_enc_len(*str);

  if(!len) return 0;

  ucs4 = *str++ & mask[len - 1];

  while(--len) {
    if((*str >> 6) != 2) return 0;
    ucs4 <<= 6;
    ucs4 += *str++ & 0x3f;
  }

  return ucs4;
}


/*
 * Encode utf8 string.
 */
unsigned char *utf8_encode(int c)
{
  static unsigned char buf[7], *s;
  unsigned char mask;
  int rmask;

  *(s = buf + sizeof buf - 1) = 0;

  if(c & ~0x7fffffff) return s;

#if 0
  fprintf(stderr, "{u+%02x:", c);
#endif

  if(c < 0x80) {
    *--s = c;
  }
  else {
    for(mask = 0x80, rmask = 0x3f; c & ~rmask; c >>= 6, rmask >>= 1) {
      *--s = (c & 0x3f) | 0x80;
      mask = (mask >> 1) + 0x80;
    }
    *--s = mask | c;
  }

#if 0
  {
    unsigned char *t;
    for(t = s; *t; t++) fprintf(stderr, " %02x", *t);
  }
  fprintf(stderr, "}");
#endif

  return s;
}


void utf8_to_utf32(int *dst, int dst_len, unsigned char *src)
{
  int i;

  while(dst_len > 1 && *src && (i = utf8_enc_len(*src))) {
    dst_len--;
    if(!(*dst++ = utf8_decode(src))) break;
    src += i;
  }

  if(dst_len > 0) *dst = 0;
}


void utf32_to_utf8(unsigned char *dst, int dst_len, int *src)
{
  int len;
  char *s;

  while(dst_len > 1 && *src && (s = utf8_encode(*src++))) {
    len = strlen(s);
    if(!len || len > dst_len - 1) break;
    dst_len -= len;
    strcpy(dst, s);
    dst += len;
  }

  if(dst_len > 0) *dst = 0;
}


int utf32_len(int *str)
{
  int len;

  for(len = 0; str[len]; len++);

  return len;
}


/*
 * Return char width (0, 1, or 2).
 */
int utf32_char_width(int c)
{
  if(c == 0) return 0;

  if(
    c >= 0x1100 &&
    (
      c <= 0x115f ||                    /* Hangul Jamo init. consonants */
      (
        c >= 0x2e80 && c <= 0xa4cf && (c & ~0x0011) != 0x300a && c != 0x303f
      ) ||                  /* CJK ... Yi */
      (c >=  0xac00 && c <=  0xd7a3) || /* Hangul Syllables */
      (c >=  0xdf00 && c <=  0xdfff) || /* dw combining sequence */
      (c >=  0xf900 && c <=  0xfaff) || /* CJK Compatibility Ideographs */
      (c >=  0xfe30 && c <=  0xfe6f) || /* CJK Compatibility Forms */
      (c >=  0xff00 && c <=  0xff5f) || /* Fullwidth Forms */
      (c >=  0xffe0 && c <=  0xffe6) ||
      (c >= 0x20000 && c <= 0x2ffff)
    )
  ) return 2;

  return 1;
}


/*
 * Return string width, taking line breaks into account.
 */
int utf8_strwidth(unsigned char *str)
{
  int i, width = 0;
  int *buf, *p, buf_len;

  buf_len = strlen(str) + 1;
  buf = malloc(buf_len * sizeof *buf);

  utf8_to_utf32(buf, buf_len, str);

  for(p = buf, i = 0; *p; p++) {
    if(*p == '\n') {
      i = 0;
    }
    else {
      i += utf32_char_width(*p);
      if(i > width) width = i;
    }
  }

  free(buf);

#if 0
  fprintf(stderr, "[%d: <%s>]\n", width, str);
#endif

  return width;
}


/*
 * Copy as long as dst doesn't exceed width. '\0' is always added.
 */
void utf8_strwcpy(unsigned char *dst, unsigned char *src, int width)
{
  int w = 0, c, l = 0;

//  fprintf(stderr, "[wcpy %d: <%s>", width, src);

  while(
    (c = utf8_decode(src + l)) &&
    (w += utf32_char_width(c)) <= width
  ) {
    l += utf8_enc_len(src[l]);
  }

  memcpy(dst, src, l);
  dst[l] = 0;

//  fprintf(stderr, " <%s>]\n", dst);
}


