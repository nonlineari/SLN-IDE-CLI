#include "inject.h"

#include <string.h>

static int url_pending(const sln_ctx *ctx)
{
    return ctx->url_act == 0 ? SLN_ACT_URL_OPEN : SLN_ACT_URL_HEAD;
}

static void add_note(sln_ctx *ctx)
{
    char buf[1200];
    char *p;
    size_t n;
    int lines = 0;

    if (!ctx->url_note[0]) {
        sln_add_str(ctx, "i edits, Esc stops, Enter runs");
        return;
    }
    n = 0;
    while (ctx->url_note[n] && n + 1 < sizeof buf) {
        buf[n] = ctx->url_note[n];
        n++;
    }
    buf[n] = 0;
    p = buf;
    while (*p && lines < 12) {
        char *nl = strchr(p, '\n');
        if (nl) {
            *nl = 0;
        }
        if (p[0]) {
            sln_add_str(ctx, p);
            lines++;
        }
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
}

static void sample(sln_ctx *ctx)
{
    if (ctx->url_act < 0 || ctx->url_act > 1) {
        ctx->url_act = 0;
    }
    sln_add(ctx, "%c open in browser", ctx->url_act == 0 ? '*' : ' ');
    sln_add(ctx, "%c curl -I", ctx->url_act == 1 ? '*' : ' ');
    ctx->cursor[ctx->pane_idx] = ctx->url_act;
    sln_add(ctx, "url %s%s", ctx->url, ctx->url_edit ? "_" : "");
    add_note(ctx);
}

static void draw(sln_ctx *ctx, WINDOW *win, int rows, int cols)
{
    sln_draw_view(ctx, win, rows, cols);
}

static void key(sln_ctx *ctx, int ch)
{
    size_t n;

    if (!ctx->url_edit) {
        if (ch == 'i') {
            ctx->url_edit = 1;
            ctx->armed = url_pending(ctx);
            return;
        }
        if (ch == 'j' || ch == KEY_DOWN) {
            ctx->url_act = 1;
            ctx->armed = SLN_ACT_URL_HEAD;
            return;
        }
        if (ch == 'k' || ch == KEY_UP) {
            ctx->url_act = 0;
            ctx->armed = SLN_ACT_URL_OPEN;
            return;
        }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            ctx->armed = url_pending(ctx);
            ctx->pending = ctx->armed;
        }
        return;
    }

    if (ch == 27) {
        ctx->url_edit = 0;
        return;
    }
    if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
        ctx->url_edit = 0;
        ctx->armed = url_pending(ctx);
        ctx->pending = ctx->armed;
        return;
    }
    n = strlen(ctx->url);
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        if (n > 0) {
            ctx->url[n - 1] = 0;
        }
        return;
    }
    if (ch == 21) {
        ctx->url[0] = 0;
        return;
    }
    if (ch >= 32 && ch < 127) {
        if (!sln_url_char_ok(ch)) {
            return;
        }
        if (n + 1 >= SLN_URL_MAX) {
            return;
        }
        ctx->url[n] = (char)ch;
        ctx->url[n + 1] = 0;
        ctx->armed = url_pending(ctx);
    }
}

const sln_pane sln_pane_url = {
    "url",
    "i edit  Enter opens externally",
    sample,
    draw,
    key
};
