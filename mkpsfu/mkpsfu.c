#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <iconv.h>
#include <errno.h>
#include <inttypes.h>

#define FONTPATH	"/usr/share/kbd/consolefonts"

#define GFX_START	0xc0
#define GFX_END		0xdf

struct option options[] = {
  { "verbose", 0, NULL, 'v' },
  { "font", 1, NULL, 'f' },
  { "add", 1, NULL, 'a' },
  { "add-text", 1, NULL, 't' },
  { "add-charset", 1, NULL, 'c' },
  { "add-font", 1, NULL, 'F' },
  { "line-height", 1, NULL, 'l' },
  { "prop", 1, NULL, 'p' },
  { "fsize", 1, NULL, 300 },
  { "gfx-char", 1, NULL, 301 },
  { "test", 0, NULL, 999 },
  { }
};

typedef struct {
  unsigned size;
  unsigned char *data;
  unsigned real_size;
} file_data_t;

typedef struct {
  uint32_t magic;
  uint16_t entries;
  uint8_t height;
  uint8_t line_height;
} font_header_t; 

typedef struct {
  char *name;
  unsigned chars;
  unsigned char *bitmap;
  unsigned unimap_len;
  unsigned char *unimap;
  unsigned height;
  int yofs;
  int height2;
  unsigned used:1;		/* font actually used */
} font_t;

typedef struct char_data_s {
  struct char_data_s* next;
  unsigned ok:1;		/* char exists */
  unsigned special_gfx:1;	/* special block drawing char to be placed between GFX_START and GFX_END */
  unsigned special_nongfx:1;	/* char that *must not* be placed between GFX_START and GFX_END */
  struct char_data_s *dup;	/* duplicate of */
  int c;			/* char (utf32) */
  int index;			/* array index for font */
  font_t *font;			/* pointer to font */
  int height;			/* char height */
  unsigned char *bitmap;	/* char bitmap, width x height */
  int new_index;		/* char index in output font */
  char *pref_font;		/* preferred font */
  int orig_c;			/* original char */
} char_data_t;

int opt_verbose = 0;
char *opt_file;
int opt_test = 0;
int opt_line_height = 0;
int opt_prop = 0;
int opt_spacing = 0;
int opt_space_width = 0;
int opt_fsize_height = 0;
int opt_fsize_yofs = 0;

file_data_t font = {};

font_t font_list[16];
int fonts;

char_data_t *char_list;

unsigned char gfx_char[0x10000];

char *pref_font = NULL;
int orig_char = -1;

static int load_font(font_t *font);
static char_data_t *add_char(int c);
static char_data_t *find_char(int c);
static void dump_char(char_data_t *cd);
static void dump_char_list(void);
static void sort_char_list(void);
static int char_sort(const void *a, const void *b);
static void locate_char(char_data_t *cd);
static int char_index(font_t *font, int c);
static void adjust_height(char_data_t *cd, int height);
static int is_special_gfx(char_data_t *cd);
static int is_special_nongfx(char_data_t *cd);
static int assign_char_pos(void);
static void add_data(file_data_t *d, void *buffer, unsigned size);
static void write_data(char *name);

int main(int argc, char **argv)
{
  int i, j, k, font_height, char_count, max_chars;
  char *str, *str1, *t, *t2;
  char_data_t *cd;
  iconv_t ic = (iconv_t) -1, ic2;
  char obuf[4], ibuf[6];
  char obuf2[4*0x100], ibuf2[0x100];
  char *obuf_ptr, *ibuf_ptr;
  size_t obuf_left, ibuf_left;
  FILE *f;
  unsigned char uc[4], *dummy_bitmap;
  font_t tmp_font;

  opterr = 0;

  while((i = getopt_long(argc, argv, "a:f:F:c:l:p:t:v", options, NULL)) != -1) {
    switch(i) {
      case 'f':
        if(fonts < sizeof font_list / sizeof *font_list) {
          font_list[fonts].yofs = opt_fsize_yofs;
          font_list[fonts].height2 = opt_fsize_height;
          font_list[fonts++].name = optarg;
        }
        break;

      case 'a':
        str = optarg;

        while((t = strsep(&str, ","))) {
          if((t2 = strchr(t, ':'))) {
            pref_font = *t ? t : NULL;
            *t2 = 0;
            t = t2 + 1;
          }
          if(sscanf(t, "%i - %i%n", &i, &j, &k) == 2 && k == strlen(t)) {
            if(i < 0 || j < 0 || j < i || j - i >= 0x10000) {
              fprintf(stderr, "invalid char range spec: %s\n", t);
              return 1;
            }
            while(i <= j) add_char(i++);
          }
          else {
            i = j = strtol(t, &str1, 0);
            if(*str1 == '=' && i >= 0) {
              t = str1 + 1;
              j = strtol(t, &str1, 0);
            }
            if(*str1 || i < 0 || j < 0) {
              fprintf(stderr, "invalid char number: %s\n", t);
              return 1;
            }
            orig_char = i;
            add_char(j);
            orig_char = -1;
          }
        }
        pref_font = NULL;
        break;

      case 'l':
        str = optarg;
        i = strtol(str, &str1, 0);
        if(*str1 || i < 0) {
          fprintf(stderr, "invalid line height: %s\n", str);
          return 1;
        }
        opt_line_height = i;
        break;

      case 'p':
        str = optarg;
        if(sscanf(str, "%i , %i%n", &i, &j, &k) == 2 && k == strlen(str)) {
          opt_prop = 1;
          opt_spacing = i;
          opt_space_width = j;
        }
        else {
          fprintf(stderr, "invalid spec: %s\n", str);
          return 1;
        }
        break;

      case 'c':
        ic2 = iconv_open("utf32le", optarg);
        if(ic2 == (iconv_t) -1) {
          fprintf(stderr, "don't know char set %s\ntry 'iconv --list'\n", optarg);
          return 1;
        }
        ibuf_ptr = ibuf2;
        ibuf_left = sizeof ibuf2;
        obuf_ptr = obuf2;
        obuf_left = sizeof obuf2;
        for(j = 0; j < sizeof ibuf2; j++) ibuf2[j] = j;
        iconv(ic2, &ibuf_ptr, &ibuf_left, &obuf_ptr, &obuf_left);
        for(str = obuf2; str < obuf_ptr; str += 4) {
          i = *(int *) str;
          if(i >= 0x20) add_char(i);
        }
        iconv_close(ic2);
        break;

      case 't':
        if(ic == (iconv_t) -1) {
          ic = iconv_open("utf32le", "utf8");
          if(ic == (iconv_t) -1) {
            fprintf(stderr, "can't convert utf8 data\n");
            return 1;
          }
        }
        if((f = fopen(optarg, "r"))) {
          int ok;

          ibuf_left = 0;
          while((i = fread(ibuf + ibuf_left, 1, sizeof ibuf - ibuf_left, f)) > 0) {
            // fprintf(stderr, "ibuf_left = %d, fread = %d\n", ibuf_left, i);
            ibuf_ptr = ibuf;
            ibuf_left += i;
            do {
              obuf_ptr = obuf;
              obuf_left = sizeof obuf;
              k = iconv(ic, &ibuf_ptr, &ibuf_left, &obuf_ptr, &obuf_left);
              // fprintf(stderr, "k = %d, errno = %d, ibuf_left = %d, obuf_left = %d\n", k, k ? errno : 0, ibuf_left, obuf_left);
              if(k >= 0 || (k == -1 && !obuf_left)) {
                ok = 1;
                if(!obuf_left) {
                  i = *(int *) obuf;
                  if(i >= 0x20) {
                    // fprintf(stderr, "add char 0x%x\n", i);
                    add_char(i);
                  }
                }
              }
              else {
                ok = 0;
              }
            }
            while(ok && ibuf_left);
            if(k == -1 && errno == EILSEQ) {
              perror("iconv");
              return 1;
            }
            if(ibuf_left) {
              memcpy(ibuf, ibuf + sizeof ibuf - ibuf_left, ibuf_left);
            }
          }
          fclose(f);
        }
        else {
          perror(optarg);
          return 1;
        }
        break;

      case 'F':
        memset(&tmp_font, 0, sizeof tmp_font);
        tmp_font.name = optarg;
        if(!load_font(&tmp_font)) {
          fprintf(stderr, "Warning: no such font: %s\n", tmp_font.name);
          break;
        }
        for(i = 0; i < tmp_font.unimap_len; i += 2) {
          j = tmp_font.unimap[i] + (tmp_font.unimap[i + 1] << 8);
          if(j != 0xffff) add_char(j);
        }

        free(tmp_font.bitmap);
        free(tmp_font.unimap);
        break;

      case 'v':
        opt_verbose++;
        break;

      case 300:
        str = optarg;
        if(sscanf(str, "%i , %i%n", &i, &j, &k) == 2 && k == strlen(str)) {
          opt_fsize_height = i;
          opt_fsize_yofs = j;
        }
        else {
          fprintf(stderr, "invalid font size spec: %s\n", str);
          return 1;
        }
        break;

      case 301:
        str = optarg;

        while((t = strsep(&str, ","))) {
          if(sscanf(t, "%i - %i%n", &i, &j, &k) == 2 && k == strlen(t)) {
            if(i < 0 || j < 0 || j < i || j - i >= 0x10000) {
              fprintf(stderr, "invalid char range spec: %s\n", t);
              return 1;
            }
            while(i <= j) {
              if(i < sizeof gfx_char / sizeof *gfx_char) gfx_char[i] = 1;
              i++;
            }
          }
          else {
            i = strtol(t, &str1, 0);
            if(*str1 || i < 0) {
              fprintf(stderr, "invalid char number: %s\n", t);
              return 1;
            }
            if(i < sizeof gfx_char / sizeof *gfx_char) gfx_char[i] = 1;
          }
        }
        break;

      case 999:
        opt_test++;
        break;
    }
  }

  argc -= optind; argv += optind;

  if(argc != 1) {
    fprintf(stderr,
      "Usage: mkpsfu [options] newfontfile\n"
      "Combine console fonts into a new font.\n"
      "  -a, --add=[preferred_font:]first[-last][,first[-last]...]\n\tAdd chars from these ranges.\n"
      "  -a, --add=[preferred_font:]orig_char=char\n\tAdd orig_char but rename it to char.\n"
      "  -c, --add-charset=charset\n\tAdd all chars from this charset.\n"
      "  -F, --add-font=console_font\n\tAdd all chars from this font.\n"
      "  -f, --font=console_font\n\tUse this font.\n"
      "  -h, --help\n\tShow this help text.\n"
      "  -t, --add-text=samplefile\n\tAdd all chars used in this file. File must be UTF-8 encoded.\n"
      "  -v, --verbose\n\tDump font info.\n"
      "      --fsize=height,yofs\n\tOverride font size.\n"
      "      --gfx-char=first[-last][,first[-last]...]\n\tSpecify special graphics char ranges.\n"
    );
    return 1;
  }

  opt_file = argv[0];

  if(ic != (iconv_t) -1) iconv_close(ic);

  /* use default char list */
  if(!char_list) for(i = 0x20; i <= 0x7f; i++) add_char(i);

  /* default font */
  if(!fonts) font_list[fonts++].name = "default8x16";

  sort_char_list();

  /* open all fonts */
  for(i = 0; i < fonts; i++) {
    if(!load_font(font_list + i)) {
      fprintf(stderr, "Warning: no such font: %s\n", font_list[i].name);
    }
  }

  /* look for chars in fonts */
  for(cd = char_list; cd; cd = cd->next) locate_char(cd);

  /* get font heigth */
  for(font_height = 0, i = 0; i < fonts; i++) {
    if(font_list[i].used) {
      if(font_list[i].height2) font_list[i].height = font_list[i].height2;
      if(font_list[i].height > font_height) font_height = font_list[i].height;
    }
  }

  for(cd = char_list; cd; cd = cd->next) {
    adjust_height(cd, font_height);
  }

  printf("Char size: 8 x %d\n", font_height);

  for(i = j = 0, cd = char_list; cd; cd = cd->next) {
    if(!cd->ok) {
      printf(i ? ", " : "Missing Chars: ");
      printf("0x%04x", cd->c);
      i = 1;
    }
  }
  if(i) printf("\n");

  char_count = assign_char_pos();

  printf("Font size: %d chars\n", char_count);

  if(char_count > 512) {
    fprintf(stderr, "Error: font too large (max 512 chars)\n");
    return 1;
  }

  max_chars = char_count > 256 ? 512 : 256;

  uc[0] = 0x36;
  uc[1] = 0x04;
  uc[2] = char_count > 256 ? 3 : 2;
  uc[3] = font_height;

  add_data(&font, uc, 4);

  dummy_bitmap = calloc(1, font_height);

  for(cd = char_list; cd; cd = cd->next) {
    if(cd->ok && cd->c == 0xfffd) {
      memcpy(dummy_bitmap, cd->bitmap, font_height);
      break;
    }
  }

  for(i = 0; i < max_chars; i++) {
    for(cd = char_list; cd; cd = cd->next) {
      if(cd->ok && !cd->dup && cd->new_index == i) break;
    }
    add_data(&font, cd ? cd->bitmap : dummy_bitmap, font_height);
  }

  uc[2] = uc[3] = 0xff;
  
  for(i = 0; i < max_chars; i++) {
    for(j = 0, cd = char_list; cd; cd = cd->next) {
      if(cd->ok && cd->new_index == i) {
        uc[0] = cd->c;
        uc[1] = cd->c >> 8;
        add_data(&font, uc, 2);
        j = 1;
      }
    }
    if(!j) {
      uc[0] = 0xfd;
      uc[1] = 0xff;
      add_data(&font, uc, 2);
    }
    add_data(&font, uc + 2, 2);
  }

  if(opt_verbose) dump_char_list();

  write_data(opt_file);

  return 0;
}


/*
 * Locate and load font.
 */
int load_font(font_t *font)
{
  FILE *f;
  char *cmd = NULL;
  unsigned char head[4];
  int ok = 0;

  if(!font->name) return 0;

  asprintf(&cmd, "gunzip -c %s/%s.psfu.gz 2>/dev/null", FONTPATH, font->name);

  if(!cmd) return 0;

  if(!(f = popen(cmd, "r"))) return 0;

  if(
    fread(head, 4, 1, f) == 1 &&
    head[0] == 0x36 &&
    head[1] == 0x04 &&
    (head[2] == 2 || head[2] == 3) &&
    head[3] > 0
  ) {
    font->height = head[3];
    font->chars = head[2] == 2 ? 256 : 512;
    ok = 1;
  }

  if(ok) {
    font->bitmap = malloc(font->chars * font->height);
    if(fread(font->bitmap, font->chars * font->height, 1, f) != 1) ok = 0;
  }

  if(ok) {
    font->unimap = malloc(64*1024);
    font->unimap_len = fread(font->unimap, 1, 64*1024, f);
    if(!font->unimap_len || (font->unimap_len & 1)) {
      ok = 0;
    }
    else {
      font->unimap = realloc(font->unimap, font->unimap_len);
      if(!font->unimap) ok = 0;
    }
  }

  pclose(f);

  return ok;
}


char_data_t *add_char(int c)
{
  char_data_t *cd;

  if((cd = find_char(c))) return cd;

  cd = calloc(1, sizeof *cd);
  cd->c = c;
  cd->next = char_list;

  cd->pref_font = pref_font;

  cd->orig_c = orig_char >= 0 ? orig_char : cd->c;

  return char_list = cd;
}


char_data_t *find_char(int c)
{
  char_data_t *cd;

  for(cd = char_list; cd; cd = cd->next) {
    if(cd->c == c) return cd;
  }

  return NULL;
}


void dump_char(char_data_t *cd)
{
  int j;
  unsigned char *p, map;

  if(!cd || !cd->ok) return;

  printf(
    "Char 0x%04x (Index 0x%02x)\n  Font: %s\n  Index: 0x%02x\n  Size: 8 x %d\n",
    cd->c, cd->new_index, cd->font->name, cd->index, cd->height
  );

  if(cd->bitmap) {
    p = cd->bitmap;
    for(j = 0; j < cd->height; j++, p++) {
      printf("    |");
      for(map = 0x80; map; map >>= 1) {
        printf("%c", *p & map ? '#' : ' ');
      }
      printf("|\n");
    }
  }
}


void dump_char_list()
{
  char_data_t *cd;

  for(cd = char_list; cd; cd = cd->next) {
    dump_char(cd);
  }
}


void sort_char_list()
{
  char_data_t *cd;
  unsigned u, len;
  char_data_t **c_list;

  for(len = 0, cd = char_list; cd; cd = cd->next) len++;

  if(!len) return;

  c_list = calloc(len + 1, sizeof *c_list);

  for(u = 0, cd = char_list; cd; cd = cd->next, u++) c_list[u] = cd;

  qsort(c_list, len, sizeof *c_list, char_sort);

  for(u = 0; u < len; u++) {
    c_list[u]->next = c_list[u + 1];
  }

  char_list = *c_list;

  free(c_list);
}


int char_sort(const void *a, const void *b)
{
  int ca, cb;

  ca = (*(char_data_t **) a)->c;
  cb = (*(char_data_t **) b)->c;

  /* 0xfffd should appear first */
  if(ca != cb) {
    if(ca == 0xfffd) return -1;
    if(cb == 0xfffd) return 1;
  }

  return ca - cb;
}


void locate_char(char_data_t *cd)
{
  int i, j, k, idx;

  k = -1;

  if(cd->pref_font) {
    for(i = 0; i < fonts; i++) {
      if(!strcmp(cd->pref_font, font_list[i].name)) {
        k = i;
        break;
      }
    }
  }

  for(i = -1; i < fonts; i++) {
    idx = i == -1 ? k : i;
    if(idx < 0) continue;

    if(
      (j = char_index(font_list + idx, cd->orig_c)) >= 0 &&
      j < font_list[idx].chars
    ) {
      cd->index = j;
      cd->font = font_list + idx;
      cd->ok = 1;
      cd->bitmap = malloc(cd->height = font_list[idx].height);
      memcpy(cd->bitmap, font_list[idx].bitmap + cd->index *  cd->height, cd->height);
      font_list[idx].used = 1;
      break;
    }
  }
}


int char_index(font_t *font, int c)
{
  int i;
  unsigned u, u1;

  if(!font || !font->unimap || c < 0 || c >= 0xffff) return -1;

  for(i = u = 0; u < font->unimap_len; u += 2) {
    u1 = font->unimap[u] + (font->unimap[u + 1] << 8);
    if(u1 == 0xffff) {
      i++;
      continue;
    }
    if(u1 == c) return i;
  }

  return -1;
}


void adjust_height(char_data_t *cd, int height)
{
  int i, j, k, warned = 0;
  unsigned char *p;

  if(!cd || !cd->ok) return;

  p = calloc(1, height);

  j = height - cd->height + cd->font->yofs;
  for(i = 0; i < cd->height; i++) {
    k = i + j;

    if(k < 0 || k >= height) {
      if(cd->bitmap[i] && !warned){
        printf("Warning: u+%04x has been clipped\n", cd->c);
        warned = 1;
      }
    }
    else {
      p[k] = cd->bitmap[i];
    }
  }

  cd->height = height;

  free(cd->bitmap);

  cd->bitmap = p;
}


/*
 * Char *must* be placed in GFX_START - GFX_END range.
 */
int is_special_gfx(char_data_t *cd)
{
  int i;
  unsigned char uc;

  if(
    cd->c < 0 ||
    cd->c >= sizeof gfx_char / sizeof *gfx_char ||
    !gfx_char[cd->c]
  ) return 0;

  for(uc = 0, i = 0; i < cd->height; i++) {
    uc |= cd->bitmap[i];
  }

  if(uc & 1) return 1;

  return 0;
}


/*
 * Char *must not* be placed in GFX_START - GFX_END range.
 */
int is_special_nongfx(char_data_t *cd)
{
  int i;
  unsigned char uc;

  if(
    cd->c >= 0 &&
    cd->c < sizeof gfx_char / sizeof *gfx_char &&
    gfx_char[cd->c]
  ) return 0;

  for(uc = 0, i = 0; i < cd->height; i++) {
    uc |= cd->bitmap[i];
  }

  if(uc & 1) return 1;

  return 0;
}


/*
 * Assign char position in output font.
 *
 * Returns number of chars in font.
 */
int assign_char_pos()
{
  int char_count, char_index, i;
  char_data_t *cd, *cd2;
  char_data_t *used[512] = { };
  int max_chars;

  for(cd = char_list; cd; cd = cd->next) {
    cd->new_index = -1;
  }

  /* first, find identical bitmaps */
  for(cd = char_list; cd; cd = cd->next) {
    if(!cd->ok || cd->dup) continue;

    for(cd2 = cd->next; cd2; cd2 = cd2->next) {
      if(!cd2->ok || cd2->dup) continue;
      if(!memcmp(cd->bitmap, cd2->bitmap, cd->height)) {
        cd2->dup = cd;
      }
    }
  }

  char_count = 0;
  for(cd = char_list; cd; cd = cd->next) {
    if(cd->ok && !cd->dup) char_count++;
  }

  if(char_count > 512) return char_count;

  max_chars = char_count > 256 ? 512 : 256;

  for(cd = char_list; cd; cd = cd->next) {
    if(!cd->ok) continue;

    if(is_special_gfx(cd)) {
      cd->special_gfx = 1;
      if(cd->dup) cd->dup->special_gfx = 1;
    }

    if(is_special_nongfx(cd)) {
      cd->special_nongfx = 1;
      if(cd->dup) cd->dup->special_nongfx = 1;
    }
  }

  /* first, try default position */

  for(cd = char_list; cd; cd = cd->next) {
    if(!cd->ok || cd->dup || cd->new_index >= 0) continue;

    if(cd->c == 0xfffd && !used[0]) {
      cd->new_index = 0;
      used[0] = cd;
      continue;
    }

    if(cd->c >= 1 && cd->c < max_chars && !used[cd->c]) {
      used[cd->new_index = cd->c] = cd;
    }
  }

  /* delete assignments that don't work */

  for(cd = char_list; cd; cd = cd->next) {
    if(!cd->ok || cd->dup || cd->new_index == -1) continue;

    if(
      cd->special_gfx &&
      (cd->new_index < GFX_START || cd->new_index > GFX_END)
    ) {
      used[cd->new_index] = 0;
      cd->new_index = -1;
      continue;
    }

    if(
      cd->special_nongfx &&
      (cd->new_index >= GFX_START && cd->new_index <= GFX_END)
    ) {
      used[cd->new_index] = 0;
      cd->new_index = -1;
    }
  }

  /* go for gfx chars */

  for(cd = char_list; cd; cd = cd->next) {
    if(!cd->ok || cd->dup || cd->new_index >= 0 || !cd->special_gfx) continue;

    for(i = GFX_START; i <= GFX_END; i++) {
      if(!used[i]) break;
    }

    if(i <= GFX_END) {
      used[cd->new_index = i] = cd;
      continue;
    }

    for(i = GFX_START; i <= GFX_END; i++) {
      if(used[i] && !used[i]->special_gfx) break;
    }

    if(i <= GFX_END) {
      if(used[i]) used[i]->new_index = -1;
      used[cd->new_index = i] = cd;
    }
  }

  /* go for nongfx chars */

  for(cd = char_list; cd; cd = cd->next) {
    if(!cd->ok || cd->dup || cd->new_index >= 0 || !cd->special_nongfx) continue;

    for(i = 0; i < max_chars; i++) {
      if(i >= GFX_START && i <= GFX_END) continue;

      if(!used[i]) break;
    }

    if(i < max_chars) {
      used[cd->new_index = i] = cd;
      continue;
    }

    for(i = 0; i < max_chars; i++) {
      if(i >= GFX_START && i <= GFX_END) continue;

      if(used[i] && !used[i]->special_nongfx) break;
    }

    if(i < max_chars) {
      if(used[i]) used[i]->new_index = -1;
      used[cd->new_index = i] = cd;
    }
  }

  /* now the remaining chars */

  char_index = 0;

  for(cd = char_list; cd; cd = cd->next) {
    if(!cd->ok || cd->dup || cd->new_index >= 0) continue;

    for(; char_index < max_chars; char_index++) {
      if(!used[char_index]) {
        cd->new_index = char_index;
        used[cd->new_index = char_index] = cd;
        break;
      }
    }
  }

  /* sync duplicates */

  for(cd = char_list; cd; cd = cd->next) {
    if(cd->ok && cd->dup) cd->new_index = cd->dup->new_index;
  }

  return char_count;
}


void add_data(file_data_t *d, void *buffer, unsigned size)
{
  if(!size || !d || !buffer) return;

  if(d->size + size > d->real_size) {
    d->real_size = d->size + size + 0x1000;
    d->data = realloc(d->data, d->real_size);
    if(!d->data) d->real_size = 0;
  }

  if(d->size + size <= d->real_size) {
    memcpy(d->data + d->size, buffer, size);
    d->size += size;
  }
  else {
    fprintf(stderr, "Oops, out of memory? Aborted.\n");
    exit(10);
  }
}


void write_data(char *name)
{
  FILE *f;

  f = strcmp(name, "-") ? fopen(name, "w") : stdout;

  if(!f) {
    perror(name);
    return;
  }

  if(fwrite(font.data, font.size, 1, f) != 1) {
    perror(name); exit(3);
  }

  fclose(f);
}


