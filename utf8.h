int utf8_enc_len(int first_char);
int utf8_decode(unsigned char *str);
unsigned char *utf8_encode(int c);
void utf8_to_utf32(int *dst, int dst_len, unsigned char *src);
void utf32_to_utf8(unsigned char *dst, int dst_len, int *src);
int utf32_len(int *str);
int utf32_char_width(int c);
int utf8_strwidth(unsigned char *str);
void utf8_strwcpy(unsigned char *dst, unsigned char *src, int width);

