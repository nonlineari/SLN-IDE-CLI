#include "inject.h"

static int seen_seq = -1;

static const char *armed_name(const sln_ctx *ctx)
{
    if (ctx->armed == SLN_ACT_URL_OPEN) {
        return "xdg-open";
    }
    if (ctx->armed == SLN_ACT_URL_HEAD) {
        return "curl -I";
    }
    if (ctx->esp_which == 1) {
        return "esp.sh flash";
    }
    if (ctx->esp_which == 2) {
        return "esp.sh monitor";
    }
    return "esp.sh build";
}

static void sample(sln_ctx *ctx)
{
    int pane = ctx->pane_idx;

    sln_add(ctx, "enter runs %s", armed_name(ctx));
    sln_add_str(ctx, "keys are not a shell");
    if (ctx->last_cmd[0]) {
        sln_add(ctx, "last %s", ctx->last_cmd);
    } else {
        sln_add_str(ctx, "last (none)");
    }
    if (ctx->have_exit) {
        sln_add(ctx, "exit %d", ctx->last_exit);
    } else {
        sln_add_str(ctx, "exit -");
    }
    sln_add_log_tail(ctx, SLN_ESP_LOG, 16);
    if (seen_seq != ctx->log_seq && ctx->nview[pane] > 0) {
        seen_seq = ctx->log_seq;
        ctx->cursor[pane] = ctx->nview[pane] - 1;
    }
}

static void draw(sln_ctx *ctx, WINDOW *win, int rows, int cols)
{
    sln_draw_view(ctx, win, rows, cols);
}

static void key(sln_ctx *ctx, int ch)
{
    if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
        if (ctx->armed == SLN_ACT_URL_OPEN || ctx->armed == SLN_ACT_URL_HEAD) {
            ctx->pending = ctx->armed;
        } else {
            ctx->armed = SLN_ACT_ESP;
            ctx->pending = SLN_ACT_ESP;
        }
        return;
    }
    sln_key_scroll(ctx, ch);
}

const sln_pane sln_pane_cmdline = {
    "cmdline",
    "Enter repeats the armed action",
    sample,
    draw,
    key
};
