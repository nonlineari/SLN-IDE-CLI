#include "inject.h"

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void sample(sln_ctx *ctx)
{
    struct stat st;
    struct tm tm;
    char when[32];
    const char *kind = "other";
    char mode[8];
    struct passwd *pw;
    struct group *gr;

    sln_add(ctx, "%s", ctx->selected);
    if (lstat(ctx->selected, &st) != 0) {
        sln_add_str(ctx, "cannot stat");
        return;
    }
    if (S_ISREG(st.st_mode)) {
        kind = "file";
    } else if (S_ISDIR(st.st_mode)) {
        kind = "directory";
    } else if (S_ISLNK(st.st_mode)) {
        kind = "symlink";
    }
    snprintf(mode, sizeof mode, "%04o", (unsigned)(st.st_mode & 07777));
    pw = getpwuid(st.st_uid);
    gr = getgrgid(st.st_gid);
    when[0] = 0;
    if (localtime_r(&st.st_mtime, &tm)) {
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
    }
    sln_add(ctx, "type %s", kind);
    sln_add(ctx, "mode %s", mode);
    sln_add(ctx, "owner %s", pw && pw->pw_name ? pw->pw_name : "?");
    sln_add(ctx, "group %s", gr && gr->gr_name ? gr->gr_name : "?");
    sln_add(ctx, "uid %ld  gid %ld", (long)st.st_uid, (long)st.st_gid);
    sln_add(ctx, "size %lld", (long long)st.st_size);
    sln_add(ctx, "mtime %s", when[0] ? when : "?");
}

static void draw(sln_ctx *ctx, WINDOW *win, int rows, int cols)
{
    sln_draw_view(ctx, win, rows, cols);
}

static void key(sln_ctx *ctx, int ch)
{
    sln_key_scroll(ctx, ch);
}

const sln_pane sln_pane_files = {
    "files",
    "stat of the tree selection",
    sample,
    draw,
    key
};
