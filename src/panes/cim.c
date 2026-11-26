#include "inject.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>

static void copy_strip(char *dst, size_t n, const char *src)
{
    size_t i = 0;

    if (n == 0) {
        return;
    }
    while (src[i] && src[i] != '\n' && src[i] != '\r' && i + 1 < n) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void sample(sln_ctx *ctx)
{
    struct utsname uts;
    struct stat st;
    struct tm tm;
    char vendor[64];
    char product[64];
    char version[64];
    char model[160];
    char load[128];
    char id_vendor[16];
    char id_product[16];
    char usb_text[96];
    char when[32];
    FILE *cpu;
    char line[256];

    vendor[0] = product[0] = version[0] = model[0] = 0;
    sln_add_str(ctx, "ComputerSystem");
    if (sln_read_str("/sys/class/dmi/id/sys_vendor", vendor, sizeof vendor) != 0) {
        vendor[0] = 0;
    }
    if (sln_read_str("/sys/class/dmi/id/product_name", product, sizeof product) != 0) {
        product[0] = 0;
    }
    if (sln_read_str("/sys/class/dmi/id/product_version", version, sizeof version) != 0) {
        version[0] = 0;
    }
    sln_add(ctx, "  vendor %s", vendor[0] ? vendor : "?");
    sln_add(ctx, "  product %s", product[0] ? product : "?");
    sln_add(ctx, "  version %s", version[0] ? version : "?");
    if (uname(&uts) == 0) {
        sln_add(ctx, "  node %s", uts.nodename);
        sln_add(ctx, "  kernel %s %s", uts.sysname, uts.release);
        sln_add(ctx, "  machine %s", uts.machine);
    }

    sln_add_str(ctx, "Processor");
    cpu = fopen("/proc/cpuinfo", "r");
    if (cpu) {
        while (fgets(line, (int)sizeof line, cpu)) {
            char *colon;
            if (strncmp(line, "model name", 10) != 0) {
                continue;
            }
            colon = strchr(line, ':');
            if (!colon) {
                break;
            }
            colon++;
            while (*colon == ' ' || *colon == '\t') {
                colon++;
            }
            copy_strip(model, sizeof model, colon);
            break;
        }
        fclose(cpu);
    }
    sln_add(ctx, "  model %s", model[0] ? model : "?");
    if (sln_read_str("/proc/loadavg", load, sizeof load) == 0) {
        sln_add(ctx, "  load %s", load);
    }

    sln_add_str(ctx, "TemperatureSensor");
    sln_sample_temps(ctx, "  ", 6, 0);

    sln_add_str(ctx, "LogicalFile");
    sln_add(ctx, "  %s", ctx->selected);
    if (lstat(ctx->selected, &st) == 0) {
        sln_add(ctx, "  size %lld", (long long)st.st_size);
    } else {
        sln_add_str(ctx, "  unreadable");
    }

    sln_add_str(ctx, "USBDevice");
    if (sln_usb_identity(id_vendor, sizeof id_vendor, id_product, sizeof id_product,
                         usb_text, sizeof usb_text) == 0) {
        sln_add(ctx, "  ttyUSB0 %s:%s", id_vendor, id_product);
        if (usb_text[0]) {
            sln_add(ctx, "  %s", usb_text);
        }
    } else {
        sln_add_str(ctx, "  ttyUSB0 unread");
    }

    sln_add_str(ctx, "ESP32Board");
    sln_add_str(ctx, "  port /dev/ttyUSB0");
    sln_add(ctx, "  %s", SLN_FW_BIN);
    if (stat(SLN_FW_BIN, &st) == 0) {
        sln_add(ctx, "  size %lld", (long long)st.st_size);
        when[0] = 0;
        if (localtime_r(&st.st_mtime, &tm) &&
            strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm) > 0) {
            sln_add(ctx, "  mtime %s", when);
        }
    } else {
        sln_add_str(ctx, "  image missing");
    }
}

static void draw(sln_ctx *ctx, WINDOW *win, int rows, int cols)
{
    sln_draw_view(ctx, win, rows, cols);
}

static void key(sln_ctx *ctx, int ch)
{
    sln_key_scroll(ctx, ch);
}

const sln_pane sln_pane_cim = {
    "cim",
    "read-only catalog",
    sample,
    draw,
    key
};
