#include "inject.h"

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void append_group(char *dst, size_t n, const char *name)
{
    size_t u = strlen(dst);
    size_t i = 0;

    if (u && u + 1 < n) {
        dst[u++] = ' ';
        dst[u] = 0;
    }
    while (name[i] && u + 1 < n) {
        dst[u++] = name[i++];
    }
    dst[u] = 0;
}

static void sample_listeners(sln_ctx *ctx)
{
    FILE *f;
    char line[512];
    int shown = 0;

    f = fopen("/proc/net/tcp", "r");
    if (!f) {
        sln_add_str(ctx, "tcp table unreadable");
        return;
    }
    while (fgets(line, (int)sizeof line, f)) {
        char local[64];
        char remote[64];
        char state[8];
        unsigned ip = 0;
        unsigned port = 0;
        unsigned a;
        unsigned b;
        unsigned c;
        unsigned d;

        if (sscanf(line, "%*d: %63s %63s %7s", local, remote, state) != 3) {
            continue;
        }
        if (strcmp(state, "0A") != 0) {
            continue;
        }
        if (sscanf(local, "%x:%x", &ip, &port) != 2) {
            continue;
        }
        if (shown >= 12) {
            sln_add_str(ctx, "listen ...");
            break;
        }
        a = ip & 0xffu;
        b = (ip >> 8) & 0xffu;
        c = (ip >> 16) & 0xffu;
        d = (ip >> 24) & 0xffu;
        sln_add(ctx, "listen %u.%u.%u.%u:%u", a, b, c, d, port);
        shown++;
        (void)remote;
    }
    fclose(f);
    if (shown == 0) {
        sln_add_str(ctx, "listen none");
    }
}

static void sample(sln_ctx *ctx)
{
    struct passwd *pw;
    gid_t gids[64];
    int ng = 64;
    int in_uucp = 0;
    char groups[512];
    char vendor[16];
    char product[16];
    char text[128];
    struct stat st;

    pw = getpwuid(geteuid());
    if (pw) {
        sln_add(ctx, "user %s", pw->pw_name);
        sln_add(ctx, "uid %ld", (long)pw->pw_uid);
    } else {
        sln_add(ctx, "uid %ld", (long)geteuid());
    }
    groups[0] = 0;
    if (pw && getgrouplist(pw->pw_name, pw->pw_gid, gids, &ng) >= 0) {
        int i;
        for (i = 0; i < ng; i++) {
            struct group *gr = getgrgid(gids[i]);
            const char *name = (gr && gr->gr_name) ? gr->gr_name : "?";
            append_group(groups, sizeof groups, name);
            if (strcmp(name, "uucp") == 0) {
                in_uucp = 1;
            }
        }
        sln_add(ctx, "groups %s", groups);
    } else {
        sln_add_str(ctx, "groups unavailable");
    }
    sln_add(ctx, "uucp %s", in_uucp ? "member" : "not a member");

    if (stat("/dev/ttyUSB0", &st) == 0) {
        sln_add(ctx, "ttyUSB0 mode %04o", (unsigned)(st.st_mode & 07777));
    } else {
        sln_add_str(ctx, "ttyUSB0 absent");
    }
    if (access("/dev/ttyUSB0", R_OK | W_OK) == 0) {
        sln_add_str(ctx, "ttyUSB0 open");
    } else {
        sln_add_str(ctx, "ttyUSB0: cannot open (group uucp required)");
    }

    if (sln_usb_identity(vendor, sizeof vendor, product, sizeof product, text, sizeof text) == 0) {
        sln_add(ctx, "usb %s:%s", vendor, product);
        if (text[0]) {
            sln_add(ctx, "usb %s", text);
        }
    } else {
        sln_add_str(ctx, "usb identity unread");
    }
    sample_listeners(ctx);
}

static void draw(sln_ctx *ctx, WINDOW *win, int rows, int cols)
{
    sln_draw_view(ctx, win, rows, cols);
}

static void key(sln_ctx *ctx, int ch)
{
    sln_key_scroll(ctx, ch);
}

const sln_pane sln_pane_security = {
    "security",
    "read-only identity and port",
    sample,
    draw,
    key
};
