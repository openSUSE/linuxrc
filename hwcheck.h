typedef struct {
  char *vendor, *model, *driver;
} scanner_t;

static scanner_t scanner_data[] = {
  { "Abaton", "SCAN 300/GS", "abaton" },
  { "Abaton", "SCAN 300/S", "abaton" },
  { "Acer", "300f", "SnapScan" },
  { "Acer", "310s", "SnapScan" },
  { "Acer", "610plus", "SnapScan" },
  { "Acer", "610s", "SnapScan" },
  { "Acer", "Prisa 1240", "SnapScan" },
  { "Acer", "Prisa 3300", "SnapScan" },
  { "Acer", "Prisa 4300", "SnapScan" },
  { "Acer", "Prisa 5300", "SnapScan" },
  { "Acer", "Prisa 620s", "SnapScan" },
  { "Acer", "Prisa 620u", "SnapScan" },
  { "Acer", "Prisa 620ut", "SnapScan" },
  { "Acer", "Prisa 640bu", "SnapScan" },
  { "Acer", "Prisa 640u", "SnapScan" },
  { "Agfa", "Arcus II", "microtek" },
  { "Agfa", "DuoScan", "microtek" },
  { "Agfa", "FOCUS COLOR", "agfafocus" },
  { "Agfa", "FOCUS GS SCANNER", "agfafocus" },
  { "Agfa", "FOCUS II", "agfafocus" },
  { "Agfa", "FOCUS LINEART SCANNER", "agfafocus" },
  { "Agfa", "SnapScan 1212u", "SnapScan" },
  { "Agfa", "SnapScan 1236s", "SnapScan" },
  { "Agfa", "SnapScan 1236u", "SnapScan" },
  { "Agfa", "SnapScan 300", "SnapScan" },
  { "Agfa", "SnapScan 310", "SnapScan" },
  { "Agfa", "SnapScan 600", "SnapScan" },
  { "Agfa", "SnapScan e20", "SnapScan" },
  { "Agfa", "SnapScan e25", "SnapScan" },
  { "Agfa", "SnapScan e40", "SnapScan" },
  { "Agfa", "SnapScan e50", "SnapScan" },
  { "Agfa", "SnapScan e60", "SnapScan" },
  { "Agfa", "StudioScan", "microtek" },
  { "Agfa", "StudioScan II", "microtek" },
  { "Agfa", "StudioScan IIsi", "microtek" },
  { "Apple", "APPLE SCANNER", "apple" },
  { "Apple", "COLORONESCANNER", "apple" },
  { "Apple", "ONESCANNER", "apple" },
  { "Artec", "A6000C", "artec" },
  { "Artec", "A6000C PLUS", "artec" },
  { "Artec", "AM12S", "artec" },
  { "Artec", "AT12", "artec" },
  { "Artec", "AT3", "artec" },
  { "Artec", "AT6", "artec" },
  { "Artec", "ColorOneScanner", "artec" },
  { "Avision", "AV 620 CS", "avision" },
  { "Avision", "AV 6240", "avision" },
  { "Avision", "AV 630 CS", "avision" },
  { "B&H SCSI", "COPISCAN II 2135", "bh" },
  { "B&H SCSI", "COPISCAN II 2137", "bh" },
  { "B&H SCSI", "COPISCAN II 2137A", "bh" },
  { "B&H SCSI", "COPISCAN II 2138A", "bh" },
  { "B&H SCSI", "COPISCAN II 3238", "bh" },
  { "B&H SCSI", "COPISCAN II 3338", "bh" },
  { "B&H SCSI", "COPISCAN II 6338", "bh" },
  { "BlackWidow", "BW4800SP", "artec" },
  { "Canon", "CANOSCAN 2700F", "canon" },
  { "Canon", "CANOSCAN 300", "canon" },
  { "Canon", "CANOSCAN 600", "canon" },
  { "Devcom", "9636PRO", "pie" },
  { "Devcom", "9636S", "pie" },
  { "EDGE", "KTX-9600US", "umax" },
  { "Epson", "ES-8500", "epson" },
  { "Epson", "EXPRESSION 1600", "epson" },
  { "Epson", "EXPRESSION 1680", "epson" },
  { "Epson", "EXPRESSION 636", "epson" },
  { "Epson", "EXPRESSION 800", "epson" },
  { "Epson", "FILMSCAN 200", "epson" },
  { "Epson", "GT-5500", "epson" },
  { "Epson", "GT-7000", "epson" },
  { "Epson", "GT-8000", "epson" },
  { "Epson", "PERFECTION 1200PHOTO", "epson" },
  { "Epson", "PERFECTION 1200S", "epson" },
  { "Epson", "PERFECTION 1200U", "epson" },
  { "Epson", "PERFECTION 1240", "epson" },
  { "Epson", "PERFECTION 1640", "epson" },
  { "Epson", "PERFECTION 1650", "epson" },
  { "Epson", "PERFECTION 610", "epson" },
  { "Epson", "PERFECTION 636S", "epson" },
  { "Epson", "PERFECTION 636U", "epson" },
  { "Epson", "PERFECTION 640", "epson" },
  { "Epson", "PERFECTION1200", "epson" },
  { "Epson", "Perfection 600", "umax" },
  { "Escom", "Image Scanner 256", "umax" },
  { "Escort", "Galleria 600", "umax" },
  { "Fujitsu", "M3091DCD", "m3091" },
  { "Fujitsu", "M3096G", "m3096g" },
  { "Fujitsu", "SP15C", "sp15c" },
  { "Genius", "ColorPage-HR5 Pro", "umax" },
  { "Guillemot", "Maxi Scan A4 Deluxe", "SnapScan" },
  { "HP", "HP OFFICEJET K SERIES", "hp" },
  { "HP", "HP OFFICEJET V SERIES", "hp" },
  { "HP", "HP PHOTOSMART PHOTOSCANNER", "hp" },
  { "HP", "HP PSC 700 SERIES", "hp" },
  { "HP", "HP PSC 900 SERIES", "hp" },
  { "HP", "HP SCANJET 3C", "hp" },
  { "HP", "HP SCANJET 3P", "hp" },
  { "HP", "HP SCANJET 4100C", "hp" },
  { "HP", "HP SCANJET 4C", "hp" },
  { "HP", "HP SCANJET 4P", "hp" },
  { "HP", "HP SCANJET 5200C", "hp" },
  { "HP", "HP SCANJET 6100C", "hp" },
  { "HP", "HP SCANJET 6200C", "hp" },
  { "HP", "HP SCANJET 6250C", "hp" },
  { "HP", "HP SCANJET 6300C", "hp" },
  { "HP", "HP SCANJET 6350C", "hp" },
  { "HP", "HP SCANJET 6390C", "hp" },
  { "HP", "HP SCANJET IIC", "hp" },
  { "HP", "HP SCANJET IICX", "hp" },
  { "HP", "HP SCANJET IIP", "hp" },
  { "HP", "HP ScanJet 5p", "hp" },
  { "HP", "HP4200", "hp4200" },
  { "Highscreen", "Scanboostar Premium", "umax" },
  { "Linotype Hell", "Jade", "umax" },
  { "Linotype Hell", "Jade2", "umax" },
  { "Linotype Hell", "Linoscan 1400", "umax" },
  { "Linotype Hell", "Opal", "umax" },
  { "Linotype Hell", "Opal Ultra", "umax" },
  { "Linotype Hell", "Saphir", "umax" },
  { "Linotype Hell", "Saphir HiRes", "umax" },
  { "Linotype Hell", "Saphir Ultra", "umax" },
  { "Linotype Hell", "Saphir Ultra II", "umax" },
  { "Linotype Hell", "Saphir2", "umax" },
  { "Microtek", "Phantom 636", "microtek2" },
  { "Microtek", "ScanMaker 330", "microtek2" },
  { "Microtek", "ScanMaker 3600", "sm3600" },
  { "Microtek", "ScanMaker 630", "microtek2" },
  { "Microtek", "ScanMaker 636", "microtek2" },
  { "Microtek", "ScanMaker 9600XL", "microtek2" },
  { "Microtek", "ScanMaker E3plus", "microtek2" },
  { "Microtek", "ScanMaker V300", "microtek2" },
  { "Microtek", "ScanMaker V310", "microtek2" },
  { "Microtek", "ScanMaker V600", "microtek2" },
  { "Microtek", "ScanMaker V6USL", "microtek2" },
  { "Microtek", "ScanMaker X6", "microtek2" },
  { "Microtek", "ScanMaker X6EL", "microtek2" },
  { "Microtek", "ScanMaker X6USB", "microtek2" },
  { "Microtek", "Scanmaker 35", "microtek" },
  { "Microtek", "Scanmaker 35t+", "microtek" },
  { "Microtek", "Scanmaker 45t", "microtek" },
  { "Microtek", "Scanmaker 600G", "microtek" },
  { "Microtek", "Scanmaker 600G S", "microtek" },
  { "Microtek", "Scanmaker 600GS", "microtek" },
  { "Microtek", "Scanmaker 600S", "microtek" },
  { "Microtek", "Scanmaker 600Z", "microtek" },
  { "Microtek", "Scanmaker 600Z S", "microtek" },
  { "Microtek", "Scanmaker 600ZS", "microtek" },
  { "Microtek", "Scanmaker E2", "microtek" },
  { "Microtek", "Scanmaker E3", "microtek" },
  { "Microtek", "Scanmaker E6", "microtek" },
  { "Microtek", "Scanmaker II", "microtek" },
  { "Microtek", "Scanmaker IIG", "microtek" },
  { "Microtek", "Scanmaker IIHR", "microtek" },
  { "Microtek", "Scanmaker III", "microtek" },
  { "Microtek", "Scanmaker IISP", "microtek" },
  { "Microtek", "SlimScan C6", "microtek2" },
  { "Mustek", "1200 CU", "mustek_usb" },
  { "Mustek", "1200 CU Plus", "mustek_usb" },
  { "Mustek", "1200 UB", "mustek_usb" },
  { "Mustek", "600 CU", "mustek_usb" },
  { "Mustek", "Paragon 1200 A3 Pro", "mustek" },
  { "Mustek", "Paragon 1200 III SP", "mustek" },
  { "Mustek", "Paragon 1200 LS", "mustek" },
  { "Mustek", "Paragon 1200 SP Pro", "mustek" },
  { "Mustek", "Paragon 600 II CD", "mustek" },
  { "Mustek", "Paragon 800 II SP", "mustek" },
  { "Mustek", "Paragon MFC-600S", "mustek" },
  { "Mustek", "Paragon MFC-800S", "mustek" },
  { "Mustek", "Paragon MFS-12000CX", "mustek" },
  { "Mustek", "Paragon MFS-12000SP", "mustek" },
  { "Mustek", "Paragon MFS-1200SP", "mustek" },
  { "Mustek", "Paragon MFS-6000CX", "mustek" },
  { "Mustek", "Paragon MFS-6000SP", "mustek" },
  { "Mustek", "Paragon MFS-8000SP", "mustek" },
  { "Mustek", "ScanExpress 12000SP", "mustek" },
  { "Mustek", "ScanExpress 12000SP Plus", "mustek" },
  { "Mustek", "ScanExpress 6000SP", "mustek" },
  { "Mustek", "ScanExpress A3 SP", "mustek" },
  { "Mustek", "ScanMagic 600 II SP", "mustek" },
  { "Mustek", "ScanMagic 9636S", "mustek" },
  { "Mustek", "ScanMagic 9636S Plus", "mustek" },
  { "NEC", "PC-IN500/4C", "nec" },
  { "Nikon", "AX-210", "umax" },
  { "Nikon", "LS-1000", "coolscan" },
  { "Nikon", "LS-20", "coolscan" },
  { "Nikon", "LS-2000", "coolscan" },
  { "Nikon", "LS-30", "coolscan" },
  { "Pie", "9630S", "pie" },
  { "Pie", "ScanAce 1230S", "pie" },
  { "Pie", "ScanAce 1236S", "pie" },
  { "Pie", "ScanAce 630S", "pie" },
  { "Pie", "ScanAce 636S", "plustek" },
  { "Pie", "ScanAce II", "pie" },
  { "Pie", "ScanAce II Plus", "pie" },
  { "Pie", "ScanAce III", "pie" },
  { "Pie", "ScanAce III Plus", "pie" },
  { "Pie", "ScanAce Plus", "pie" },
  { "Pie", "ScanAce ScanMedia", "pie" },
  { "Pie", "ScanAce ScanMedia II", "pie" },
  { "Pie", "ScanAce V", "pie" },
  { "Plustek", "OpticPro 19200S", "artec" },
  { "Polaroid", "DMC", "dmc" },
  { "Ricoh", "Ricoh IS50", "ricoh" },
  { "Ricoh", "Ricoh IS60", "ricoh" },
  { "Scanport", "SQ4836", "microtek2" },
  { "Sharp", "9036 Flatbed scanner", "sharp" },
  { "Sharp", "JX-250", "sharp" },
  { "Sharp", "JX-320", "sharp" },
  { "Sharp", "JX-330", "sharp" },
  { "Sharp", "JX-350", "sharp" },
  { "Sharp", "JX-610", "sharp" },
  { "Siemens", "9036 Flatbed scanner", "s9036" },
  { "Siemens", "FOCUS COLOR PLUS", "agfafocus" },
  { "Siemens", "ST400", "st400" },
  { "Siemens", "ST800", "st400" },
  { "Tamarack", "Artiscan 12000C", "tamarack" },
  { "Tamarack", "Artiscan 6000C", "tamarack" },
  { "Tamarack", "Artiscan 8000C", "tamarack" },
  { "Trust", "Compact Scan USB 19200", "mustek_usb" },
  { "Trust", "Imagery 1200 SP", "mustek" },
  { "Trust", "Imagery 4800 SP", "mustek" },
  { "Trust", "SCSI Connect 19200", "mustek" },
  { "Trust", "SCSI excellence series 19200", "mustek" },
  { "UMAX", "Astra 1200S", "umax" },
  { "UMAX", "Astra 1220S", "umax" },
  { "UMAX", "Astra 2100S", "umax" },
  { "UMAX", "Astra 2200", "umax" },
  { "UMAX", "Astra 2200 S", "umax" },
  { "UMAX", "Astra 2200 U", "umax" },
  { "UMAX", "Astra 2400S", "umax" },
  { "UMAX", "Astra 600S", "umax" },
  { "UMAX", "Astra 610S", "umax" },
  { "UMAX", "Gemini D-16", "umax" },
  { "UMAX", "Mirage D-16L", "umax" },
  { "UMAX", "Mirage II", "umax" },
  { "UMAX", "Mirage IIse", "umax" },
  { "UMAX", "PL-II", "umax" },
  { "UMAX", "PSD", "umax" },
  { "UMAX", "PowerLook", "umax" },
  { "UMAX", "PowerLook 2000", "umax" },
  { "UMAX", "PowerLook 3000", "umax" },
  { "UMAX", "PowerLook III", "umax" },
  { "UMAX", "Supervista S-12", "umax" },
  { "UMAX", "UC 1200S", "umax" },
  { "UMAX", "UC 1200SE", "umax" },
  { "UMAX", "UC 1260", "umax" },
  { "UMAX", "UC 630", "umax" },
  { "UMAX", "UC 840", "umax" },
  { "UMAX", "UG 630", "umax" },
  { "UMAX", "UG 80", "umax" },
  { "UMAX", "UMAX S-12", "umax" },
  { "UMAX", "UMAX S-12G", "umax" },
  { "UMAX", "UMAX S-6E", "umax" },
  { "UMAX", "UMAX S-6EG", "umax" },
  { "UMAX", "UMAX VT600", "umax" },
  { "UMAX", "Vista S6", "umax" },
  { "UMAX", "Vista S6E", "umax" },
  { "UMAX", "Vista-S8", "umax" },
  { "UMAX", "Vista-T630", "umax" },
  { "Ultima", "A6000C", "artec" },
  { "Ultima", "A6000C PLUS", "artec" },
  { "Ultima", "AM12S", "artec" },
  { "Ultima", "AT12", "artec" },
  { "Ultima", "AT3", "artec" },
  { "Ultima", "AT6", "artec" },
  { "Ultima", "ColorOneScanner", "artec" },
  { "Vobis", "HighScan", "microtek2" },
  { "Vobis", "Scanboostar Premium", "umax" },
  { "Vuego", "Close SnapScan 310 compatible.", "SnapScan" }
};

static char *scanner_info(hd_t *hd)
{
  int i;

  if(!hd->vend_name || !hd->dev_name) return NULL;

  for(i = 0; i < sizeof scanner_data / sizeof *scanner_data; i++) {
    if(
      !strcasecmp(scanner_data[i].vendor, hd->vend_name) &&
      !strcasecmp(scanner_data[i].model, hd->dev_name)
    ) {
      return scanner_data[i].driver;
    }
  }

  return NULL;
}

static void hwcheck_progress(char *pos, char *msg)
{
  FILE *f = config.win ? stderr : stdout;

  fprintf(f, "\r%64s\r", "");
  fprintf(f, "> %s: %s", pos, msg);
  fflush(f);
}

static int do_hwcheck()
{
  hd_t *hd, *hd0;
  hd_res_t *res;
  driver_info_t *di, *di0;
  FILE *f;
  int i, wheels, buttons, missing = 0;
  unsigned u;
  uint64_t ul;
  char *s, *s1, *s2, *s3;
  char *sep = "\n";
  hd_hw_item_t item, items[] = {
#if 0

    hw_display, hw_monitor, hw_storage_ctrl, hw_network_ctrl, hw_tv,
    hw_sound, hw_mouse, hw_disk, hw_cdrom, hw_floppy, hw_modem, hw_isdn,
    hw_scanner, hw_camera

#else 

    hw_display, hw_monitor, hw_disk, hw_storage_ctrl, hw_keyboard, hw_mouse,
    hw_cdrom, hw_sound, hw_tv, hw_network_ctrl, hw_isdn, hw_modem,
    hw_scanner, hw_camera

#endif

  };
  hd_data_t *hd_data = calloc(1, sizeof *hd_data);

  hd_data->progress = hwcheck_progress;
  hd_data->debug = ~(HD_DEB_DRIVER_INFO | HD_DEB_HDDB);
  hd_set_probe_feature(hd_data, pr_default);

  hd_scan(hd_data);

  f = fopen("/tmp/hw_overview.log", "w");

  for(i = 0; i < sizeof items / sizeof *items; i++) {
    item = items[i];
    hd0 = hd_list(hd_data, item, 0, NULL);

#if 0

    if(!hd0) continue;
  
    switch(item) {
      case hw_disk:
        fprintf(f, "Disk\n");
        for(hd = hd0; hd; hd = hd->next) {
          u = 0;
          for(res = hd->res; res; res = res->next) {
            if(res->any.type == res_size && res->size.unit == size_unit_sectors) {
              ul = (uint64_t) res->size.val1 * (res->size.val2 ?: 0x200);
              u = ((ul >> 29) + 1) >> 1;
            }
          }
          s = hd_bus_name(hd_data, hd->bus);
          fprintf(f, "  %s", hd->model);
          if(u) {
            fprintf(f, " (");
            if(s) fprintf(f, "%s, ", s);
            fprintf(f, "%u GB)", u);
          }
          fprintf(f, "\n");
        }
        fprintf(f, "\n");
        break;

      case hw_cdrom:
        fprintf(f, "CD-ROM\n");
        for(hd = hd0; hd; hd = hd->next) {
          s = hd_bus_name(hd_data, hd->bus);
          s1 = hd_class_name(hd_data, 3, hd->base_class, hd->sub_class, hd->prog_if);
          s2 = strchr(s1, '(');
          s3 = strchr(s1, ')');
          if(s2 && s3 && s2[1] && !s3[1]) {
            *s3 = 0;
            s1 = s2 + 1;
          }
          else {
            s1 = "CD-ROM";
          }
          fprintf(f, "  %s (", hd->model);
          if(s) fprintf(f, "%s, ", s);
          fprintf(f, "%s)", s1);
          fprintf(f, "\n");
        }
        fprintf(f, "\n");
        break;

      case hw_monitor:
        fprintf(f, "Monitor\n");
        for(hd = hd0; hd; hd = hd->next) {
          s = hd->model;
          if(!strcmp(hd->unique_id, "rdCR.EY_qmtb9YY0")) s = "not detected";
          fprintf(f, "  %s\n", s);
        }
        fprintf(f, "\n");
        break;

      case hw_display:
        fprintf(f, "GFX Card\n");
        for(hd = hd0; hd; hd = hd->next) {
          di0 = hd_driver_info(hd_data, hd);
          u = 0;
          s1 = NULL;
          for(di = di0; di; di = di->next) {
            if(di->any.type == di_x11) {
              if(!s1) s1 = di->x11.server;
              if(di->x11.x3d && !u) {
                s1 = di->x11.server;
                u = 1;
              }
            }
          }
          if(!s1) {
            s1 = "not supported";
            u = 0;
          }
          fprintf(f, "  %s (%s", hd->model, s1);
          if(u) fprintf(f, ", 3D support");
          fprintf(f, ")");
          fprintf(f, "\n");
          di0 = hd_free_driver_info(di0);
        }
        fprintf(f, "\n");
        break;

      case hw_mouse:
        fprintf(f, "Mouse\n");
        for(hd = hd0; hd; hd = hd->next) {
          di0 = hd_driver_info(hd_data, hd);
          buttons = wheels = -1;	// make gcc happy
          s = NULL;
          for(di = di0; di; di = di->next) {
            if(di->any.type == di_mouse) {
              buttons = di->mouse.buttons;
              wheels = di->mouse.wheels;
              s = di->mouse.xf86;
              break;
            }
          }
          if(!s) {
            s = "not supported";
            buttons = wheels = -1;
          }
          fprintf(f, "  %s (%s", hd->model, s);
          if(buttons >= 0) fprintf(f, ", %d buttons", buttons);
          if(wheels >= 0) fprintf(f, ", %d wheels", wheels);
          fprintf(f, ")");
          fprintf(f, "\n");
          di0 = hd_free_driver_info(di0);
        }
        fprintf(f, "\n");
        break;

      case hw_tv:
        fprintf(f, "TV Card\n");
        for(hd = hd0; hd; hd = hd->next) {
          di0 = hd_driver_info(hd_data, hd);
          s = NULL;
          for(di = di0; di; di = di->next) {
            if(
              (di->any.type == di_any || di->any.type == di_module) &&
              di->any.hddb0 &&
              di->any.hddb0->str
            ) {
              s = di->any.hddb0->str;
              break;
            }
          }
          if(!s) {
            s = "not supported";
          }
          fprintf(f, "  %s (%s)\n", hd->model, s);
          di0 = hd_free_driver_info(di0);
        }
        fprintf(f, "\n");
        break;

      case hw_sound:
        fprintf(f, "Sound Card\n");
        for(hd = hd0; hd; hd = hd->next) {
          di0 = hd_driver_info(hd_data, hd);
          s = NULL;
          for(di = di0; di; di = di->next) {
            if(
              (di->any.type == di_any || di->any.type == di_module) &&
              di->any.hddb0 &&
              di->any.hddb0->str
            ) {
              s = di->any.hddb0->str;
              break;
            }
          }
          if(!s) {
            s = "not supported";
          }
          fprintf(f, "  %s (%s)\n", hd->model, s);
          di0 = hd_free_driver_info(di0);
        }
        fprintf(f, "\n");
        break;

      case hw_camera:
        fprintf(f, "Digital Camera/WebCam\n");
        for(hd = hd0; hd; hd = hd->next) {
          fprintf(f, "  %s\n", hd->model);
        }
        fprintf(f, "\n");
        break;

      case hw_floppy:
        fprintf(f, "Floppy/Zip Drive\n");
        for(hd = hd0; hd; hd = hd->next) {
          fprintf(f, "  %s\n", hd->model);
        }
        fprintf(f, "\n");
        break;

      case hw_modem:
        fprintf(f, "Modem\n");
        for(hd = hd0; hd; hd = hd->next) {
          fprintf(f, "  %s\n", hd->model);
        }
        fprintf(f, "\n");
        break;

      case hw_isdn:
        fprintf(f, "ISDN\n");
        for(hd = hd0; hd; hd = hd->next) {
          di0 = hd_driver_info(hd_data, hd);
          fprintf(f, "  %s (%ssupported)\n", hd->model, di0 ? "" : "not ");
          di0 = hd_free_driver_info(di0);
        }
        fprintf(f, "\n");
        break;

      case hw_scanner:
        fprintf(f, "Scanner\n");
        for(hd = hd0; hd; hd = hd->next) {
          s = scanner_info(hd);
          if(!s) s = "not supported";
          fprintf(f, "  %s (%s)\n", hd->model, s);
        }
        fprintf(f, "\n");
        break;

      case hw_storage_ctrl:
      case hw_network_ctrl:
        if(item == hw_storage_ctrl) {
          fprintf(f, "Storage Controller\n");
        }
        else {
          fprintf(f, "Network Controller\n");
        }
        for(hd = hd0; hd; hd = hd->next) {
          di0 = hd_driver_info(hd_data, hd);
          s = NULL;
          for(di = di0; di; di = di->next) {
            if(di->any.type == di_module && di->module.names) {
              s = di->module.names->str;
              break;
            }
          }
          if(!s) {
            if(hd->base_class == bc_storage && hd->sub_class == sc_sto_ide)
              s = "ide";
            else if(hd->base_class == bc_storage && hd->sub_class == sc_sto_floppy)
              s = "floppy";
            else {
              s = "not supported";
            }
          }
          fprintf(f, "  %s (%s)\n", hd->model, s);
          di0 = hd_free_driver_info(di0);
        }
        fprintf(f, "\n");
        break;

      default:
        break;
    }

#else

    switch(item) {
      case hw_disk:
        if(!hd0) {
          fprintf(f, "Disk     : not found\n");
          missing++;
        }
        else {
          for(hd = hd0; hd; hd = hd->next) {
            fprintf(f, "Disk     : ok    ");
            u = 0;
            for(res = hd->res; res; res = res->next) {
              if(res->any.type == res_size && res->size.unit == size_unit_sectors) {
                ul = (uint64_t) res->size.val1 * (res->size.val2 ?: 0x200);
                u = ((ul >> 29) + 1) >> 1;
              }
            }
            fprintf(f, "%s", hd->model);
            if(u) fprintf(f, " (%u GB)", u);
            fprintf(f, "\n");
          }
        }
        break;

      case hw_cdrom:
        if(!hd0) {
          fprintf(f, "CD-ROM   : not found\n");
          missing++;
        }
        else {
          for(hd = hd0; hd; hd = hd->next) {
            s1 = hd_class_name(hd_data, 3, hd->base_class, hd->sub_class, hd->prog_if);
            s2 = strchr(s1, '(');
            s3 = strchr(s1, ')');
            if(s2 && s3 && s2[1] && !s3[1]) {
              *s3 = 0;
              s1 = s2 + 1;
            }
            else {
              s1 = "CD-ROM";
            }
            fprintf(f, "%-9s: ok    %s\n", s1, hd->model);
          }
        }
        break;

      case hw_monitor:
        if(!hd0) {
          fprintf(f, "Monitor  : not found\n");
          missing++;
        }
        else {
          for(hd = hd0; hd; hd = hd->next) {
            fprintf(f, "Monitor  : ");
            if(!strcmp(hd->unique_id, "rdCR.EY_qmtb9YY0")) {
              fprintf(f, "not found\n");
              missing++;
            }
            else {
              fprintf(f, "ok    %s\n", hd->model);
            }
          }
        }
        break;

      case hw_display:
        if(!hd0) {
          fprintf(f, "Graphics : not found\n");
          missing++;
        }
        else {
          for(hd = hd0; hd; hd = hd->next) {
            fprintf(f, "Graphics : ");
            di0 = hd_driver_info(hd_data, hd);
            u = 0;
            s1 = NULL;
            for(di = di0; di; di = di->next) {
              if(di->any.type == di_x11) {
                if(!s1) s1 = di->x11.server;
                if(di->x11.x3d && !u) {
                  s1 = di->x11.server;
                  u = 1;
                }
              }
            }
            if(!s1) {
              fprintf(f, " not supported");
              u = 0;
              missing++;
            }
            else {
              fprintf(f, "ok    %s", hd->model);
              if(u) fprintf(f, " (3D supported)");
            }
            fprintf(f, "\n");
            di0 = hd_free_driver_info(di0);
          }
        }
        break;

      case hw_mouse:
        if(!hd0) {
          fprintf(f, "Mouse    : not found\n");
          missing++;
        }
        else {
          for(hd = hd0; hd; hd = hd->next) {
            fprintf(f, "Mouse    : ");
            di0 = hd_driver_info(hd_data, hd);
            buttons = wheels = -1;	// make gcc happy
            s = NULL;
            for(di = di0; di; di = di->next) {
              if(di->any.type == di_mouse) {
                buttons = di->mouse.buttons;
                wheels = di->mouse.wheels;
                s = di->mouse.xf86;
                break;
              }
            }
            if(!s) {
              fprintf(f, "not supported");
              buttons = wheels = -1;
              missing++;
            }
            else {
              fprintf(f, "ok    %s", hd->model);
              if(buttons >= 0 || wheels >= 0) fprintf(f, " (");
              if(buttons >= 0) fprintf(f, "%d buttons%s", buttons, wheels >= 0 ? ", " : "");
              if(wheels >= 0) fprintf(f, "%d wheels", wheels);
              fprintf(f, ")");
            }
            fprintf(f, "\n");
            di0 = hd_free_driver_info(di0);
          }
        }
        break;

      case hw_sound:
      case hw_tv:
        if(hd0 && item == hw_tv && sep) { fprintf(f, sep); sep = NULL; }
        if(!hd0) {
          if(item == hw_sound) {
            fprintf(f, "Sound    : not found\n");
            missing++;
          }
        }
        else {
          for(hd = hd0; hd; hd = hd->next) {
            fprintf(f, item == hw_sound ? "Sound    : " : "TV Card  : ");
            di0 = hd_driver_info(hd_data, hd);
            s = NULL;
            for(di = di0; di; di = di->next) {
              if(
                (di->any.type == di_any || di->any.type == di_module) &&
                di->any.hddb0 &&
                di->any.hddb0->str
              ) {
                s = di->any.hddb0->str;
                break;
              }
            }
            if(!s) {
              fprintf(f, "not supported\n");
              if(item == hw_sound) missing++;
            }
            else {
              fprintf(f, "ok    %s (%s)\n", hd->model, s);
            }
            di0 = hd_free_driver_info(di0);
          }
        }
        break;

      case hw_camera:
      case hw_modem:
        if(hd0 && sep) { fprintf(f, sep); sep = NULL; }
        s = item == hw_camera ? "Camera" : "Modem";
        for(hd = hd0; hd; hd = hd->next) {
          fprintf(f, "%-9s: ok    %s\n", s, hd->model);
        }
        break;

      case hw_isdn:
        if(hd0 && sep) { fprintf(f, sep); sep = NULL; }
        for(hd = hd0; hd; hd = hd->next) {
          fprintf(f, "ISDN     : ");
          di0 = hd_driver_info(hd_data, hd);
          if(!di0) {
            fprintf(f, "not supported\n");
          }
          else {
            fprintf(f, "ok    %s\n", hd->model);
          }
          di0 = hd_free_driver_info(di0);
        }
        break;

      case hw_scanner:
        if(hd0 && sep) { fprintf(f, sep); sep = NULL; }
        for(hd = hd0; hd; hd = hd->next) {
          fprintf(f, "Scanner  : ");
          s = scanner_info(hd);
          if(!s) {
            fprintf(f, "not supported\n");
          }
          else {
            fprintf(f, "ok    %s (%s)\n", hd->model, s);
          }
        }
        break;

      case hw_storage_ctrl:
        for(hd = hd0; hd; hd = hd->next) {
          if(hd->base_class == bc_storage && hd->sub_class == sc_sto_floppy) break;
        }
        if(hd) {
          fprintf(f, "Floppy   : ok\n");
        }
        else {
          fprintf(f, "Floppy   : not found\n");
          missing++;
        }
        break;

      case hw_keyboard:
        if(hd0) {
          fprintf(f, "Keyboard : ok\n");
        }
        else {
          fprintf(f, "Keyboard : not found\n");
          missing++;
        }
        break;

      case hw_network_ctrl:
        if(hd0 && sep) { fprintf(f, sep); sep = NULL; }
        for(hd = hd0; hd; hd = hd->next) {
          fprintf(f, "Network  : ");
          di0 = hd_driver_info(hd_data, hd);
          s = NULL;
          for(di = di0; di; di = di->next) {
            if(di->any.type == di_module && di->module.names) {
              s = di->module.names->str;
              break;
            }
          }
          if(!s) {
            fprintf(f, "not supported\n");
          }
          else {
            fprintf(f, "ok    %s (%s)\n", hd->model, s);
          }
          di0 = hd_free_driver_info(di0);
        }
        break;

      default:
        break;
    }

#endif


    hd_free_hd_list(hd0);

  }

  fclose(f);

  f = fopen("/tmp/hw_detail.log", "w");

  if(hd_data->log) {
    fprintf(f,
      "============ start detailed hardware log ============\n"
    );
    fprintf(f,
      "============ start debug info ============\n%s=========== end debug info ============\n",
      hd_data->log
    );
  }

  for(hd = hd_data->hd; hd; hd = hd->next) {
    hd_dump_entry(hd_data, hd, f);
  }

  fprintf(f,
    "============ end detailed hardware log ============\n"
  );

  fclose(f);

  fprintf(config.win ? stderr : stdout, "\r%64s\r", "");
  fflush(config.win ? stderr : stdout);

  hd_free_hd_data(hd_data);
  free(hd_data);

  return missing;
}


