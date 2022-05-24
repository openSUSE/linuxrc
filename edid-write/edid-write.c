// This program creates Extended Display Identification Data (EDID) conforming to EDID 1.3.
//
// For reference, see
//
// - https://vesa.org/vesa-standards
// - https://glenwing.github.io/docs/VESA-EEDID-A1.pdf
// - https://en.wikipedia.org/wiki/Extended_Display_Identification_Data
//
// To decode EDID data, there is the very nice edid-decode tool:
//
// - https://git.linuxtv.org/edid-decode.git
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <endian.h>

void help(void);
uint16_t eisa_id(const char *s);
void build_edid(void);

struct option options[] = {
  { "help",       0, NULL, 'h'  },
  { "verbose",    0, NULL, 'v'  },
  { "output",     1, NULL, 'o'  },
  { "width",      1, NULL, 1001 },
  { "height",     1, NULL, 1002 },
  { "width_mm",   1, NULL, 1003 },
  { "height_mm",  1, NULL, 1004 },
  { "h_pol",      1, NULL, 1005 },
  { "v_pol",      1, NULL, 1006 },
  { "dpi",        1, NULL, 1007 },
  { }
};

struct {
  unsigned verbose;
  unsigned width;
  unsigned height;
  unsigned width_mm;
  unsigned height_mm;
  unsigned dpi;
  unsigned h_pol:1;
  unsigned v_pol:1;
  FILE *file;
} opt;

uint8_t edid[128];

int main(int argc, char **argv)
{
  int i;

  opt.file = stdout;
  opt.width = 1024;
  opt.height = 768;
  opt.dpi = 96;

  opt.h_pol = 1;
  opt.v_pol = 0;

  opterr = 0;

  if(argc == 1) {
    help();
    return 1;
  }

  while((i = getopt_long(argc, argv, "hvo:", options, NULL)) != -1) {
    switch(i) {
      case 'v':
        opt.verbose++;
        break;

      case 'o':
        if(strcmp(optarg, "-")) {
          opt.file = fopen(optarg, "w");
          if(!opt.file) {
            perror(optarg);
            return 1;
          }
        }
        else {
          opt.file = stdout;
        }
        break;

      case 1001:
        opt.width = strtoul(optarg, NULL, 0);
        break;

      case 1002:
        opt.height = strtoul(optarg, NULL, 0);
        break;

      case 1003:
        opt.width_mm = strtoul(optarg, NULL, 0);
        break;

      case 1004:
        opt.height_mm = strtoul(optarg, NULL, 0);
        break;

      case 1005:
        opt.h_pol = strtoul(optarg, NULL, 0);
        break;

      case 1006:
        opt.v_pol = strtoul(optarg, NULL, 0);
        break;

      case 1007:
        opt.dpi = strtoul(optarg, NULL, 0);
        break;

      default:
        help();
        return i == 'h' ? 0 : 1;
    }
  }

  if(!opt.width_mm || !opt.height_mm) {
    opt.width_mm = opt.width * 25.4 / opt.dpi + 0.5;
    opt.height_mm = opt.height * 25.4 / opt.dpi + 0.5;
  }

  build_edid();

  fwrite(edid, sizeof edid, 1, opt.file);

  fclose(opt.file);

  return 0;
}

void help()
{
  printf(
    "Write EDID block\n"
    "Usage: edid-write [OPTIONS]\n"
    "Construct EDID 1.3 data block according to arguments.\n"
    "\n"
    "Options:\n"
    "  --width WIDTH\n"
    "      Monitor width in pixel.\n"
    "  --height HEIGHT\n"
    "      Monitor height in pixel.\n"
    "  --width_mm WIDTH_MM\n"
    "      Monitor width in millimeter. This overrides any DPI setting.\n"
    "  --height_mm HEIGHT_MM\n"
    "      Monitor height in millimeter. This overrides any DPI setting.\n"
    "  --h_pol 0|1\n"
    "      Horizontal sync polarity.\n"
    "  --v_pol 0|1\n"
    "      Vertical sync polarity.\n"
    "  --dpi DPI\n"
    "      Monitor resolution in dpi (default 96).\n"
    "  -o, --output FILE\n"
    "      Write result to FILE (default to stdout).\n"
    "  -v, --verbose\n"
    "      Show more log messages. Repeat option to show even more.\n"
    "  -h, --help\n"
    "      Show this text.\n"
  );
}

// Convert 3-letter EISA (aka ISA PnPID) to 16 bit number.
//
uint16_t eisa_id(const char *s)
{
  unsigned u = 0;

  for(int i = 0; i < 3; i++) {
    u <<= 5;
    if(s[i] < 'A' - 1 || s[i] > 'A' - 1 + 0x1f) return 0;
    u += s[i] - 'A' + 1;
  }

  return htobe16(u);
}

// Construct 128 byte EDID block.
//
// Input values are taken from the global opt struct.
//
// Output goes to global edid[] array.
//
void build_edid()
{
  *(uint32_t *) (edid + 0x00) = htobe32(0xffffff);		// magic
  *(uint32_t *) (edid + 0x04) = htole32(0xffffff);		// magic
  *(uint16_t *) (edid + 0x08) = htole16(eisa_id("EMU"));	// vendor id
  *(uint16_t *) (edid + 0x0a) = htole16(opt.width);		// model id
  *(uint32_t *) (edid + 0x0c) = htole32(123456789);		// serial
  edid[0x10]                  = 11;				// week of manufacture
  edid[0x11]                  = 32;				// year of manufacture (0 = 1990)
  edid[0x12]                  = 1;				// version
  edid[0x13]                  = 3;				// revision
  edid[0x14]                  = 0x80;				// digital display
  edid[0x15]                  = (opt.width_mm + 5) / 10;	// width in cm
  edid[0x16]                  = (opt.height_mm + 5) / 10;	// height in cm
  edid[0x17]                  = 0;				// gamma = 1.0
  edid[0x18]                  = 0xca;				// features: standby + suspend + color display + detailed timing block

  // 0x19 - 0x22: 10 bytes color specs; e.g.
  // 7b a1 ae 4f 44 a9 26 0c 50 54

  // 3 bytes established timings
  edid[0x23]                  = 0x21;				// 640 x 480 @ 60Hz, 800 x 600 @ 60Hz
  edid[0x24]                  = 0x08;				// 1024 x 768 @ 60Hz
  edid[0x25]                  = 0x00;

  // 8x standard timings: no timing data
  *(uint16_t *) (edid + 0x26) = 0x101;
  *(uint16_t *) (edid + 0x28) = 0x101;
  *(uint16_t *) (edid + 0x2a) = 0x101;
  *(uint16_t *) (edid + 0x2c) = 0x101;
  *(uint16_t *) (edid + 0x2e) = 0x101;
  *(uint16_t *) (edid + 0x30) = 0x101;
  *(uint16_t *) (edid + 0x32) = 0x101;
  *(uint16_t *) (edid + 0x34) = 0x101;

  // just some plausible values
  unsigned h_blank = 160;
  unsigned v_blank = 64;

  // horizontal refresh rate and pixel clock in Hz
  unsigned h_freq = (opt.height + v_blank) * 60;
  unsigned pix_freq = (opt.width + h_blank) * h_freq;

  // detailed timings
  *(uint16_t *) (edid + 0x36) = htole16(pix_freq / 10000);
  edid[0x38]                  = opt.width;			// h active
  edid[0x39]                  = h_blank;			// h blank
  edid[0x3a]                  = (opt.width >> 8) << 4;
  edid[0x3b]                  = opt.height;			// v active
  edid[0x3c]                  = v_blank;			// v blank
  edid[0x3d]                  = (opt.height >> 8) << 4;
  edid[0x3e]                  = 48;				// h sync start
  edid[0x3f]                  = 32;				// h sync width
  edid[0x40]                  = (10 << 4) + 6;			// v sync start + width
  edid[0x41]                  = 0;
  edid[0x42]                  = opt.width_mm;			// width in mm, lower 8 bits
  edid[0x43]                  = opt.height_mm;			// height in mm, lower 8 bits
  edid[0x44]                  = ((opt.width_mm >> 8) << 4) + ((opt.height_mm >> 8) & 0xf);	// width + height, upper 4 bits
  edid[0x45]                  = 0;				// h border
  edid[0x46]                  = 0;				// v border
  edid[0x47]                  = 0x18 + opt.v_pol * 4 + opt.h_pol * 2;	// polarity

  // frequency ranges
  *(uint32_t *) (edid + 0x48) = htobe32(0xfd);			// magic: range data
  edid[0x4c]                  = 0;
  edid[0x4d]                  = 40;				// min v freq in Hz
  edid[0x4e]                  = 70;				// max v freq in Hz
  edid[0x4f]                  = 30;				// min h freq in kHz
  edid[0x50]                  = h_freq / 1000 + 11;		// max h freq in kHz
  edid[0x51]                  = pix_freq / 10000000 + 1;	// max pixel clock in 10 MHz

  // monitor name
  *(uint32_t *) (edid + 0x5a) = htobe32(0xfc);			// magic: monitor name
  edid[0x5e]                  = 0;
  memcpy(edid + 0x5f, "FooBar 3000\n ", 13);			// exactly 13 bytes, padded with 1 newline and spaces

  // serial id
  *(uint32_t *) (edid + 0x6c) = htobe32(0xff);			// magic: serial id
  edid[0x70]                  = 0;
  memcpy(edid + 0x71, "123456789\n   ", 13);			// exactly 13 bytes, padded with 1 newline and spaces

  // calculate checksum
  for(int i = 0; i < sizeof edid - 1; i++) {
    edid[sizeof edid - 1] -= edid[i];
  }
}
