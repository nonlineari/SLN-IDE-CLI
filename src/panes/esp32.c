#include "inject.h"

#include <sys/stat.h>

static void sample(sln_ctx *ctx)
{
    struct stat st;
    const char *actions[] = {"build", "flash", "monitor"};
    int at;
    int a;

    if (ctx->esp_which < 0 || ctx->esp_which > 2) {
        ctx->esp_which = 0;
    }
    sln_add_str(ctx, "noiseprotocol_classic.bin");
    if (stat(SLN_FW_BIN, &st) == 0) {
        sln_add(ctx, "%lld bytes", (long long)st.st_size);
    } else {
        sln_add_str(ctx, "image missing");
    }
    sln_add(ctx, "%s", SLN_FW_BIN);
    if (ctx->have_exit && ctx->last_cmd[0]) {
        sln_add(ctx, "last %s", ctx->last_cmd);
        sln_add(ctx, "exit %d", ctx->last_exit);
    } else {
        sln_add_str(ctx, "last (none)");
    }
    sln_add_log_tail(ctx, SLN_ESP_LOG, 8);
    at = ctx->nview[ctx->pane_idx];
    for (a = 0; a < 3; a++) {
        sln_add(ctx, "%c %s", a == ctx->esp_which ? '*' : ' ', actions[a]);
    }
    ctx->cursor[ctx->pane_idx] = at + ctx->esp_which;
}

static void draw(sln_ctx *ctx, WINDOW *win, int rows, int cols)
{
    sln_draw_view(ctx, win, rows, cols);
}

static void key(sln_ctx *ctx, int ch)
{
    if (ch == 'j' || ch == KEY_DOWN) {
        ctx->esp_which = (ctx->esp_which + 1) % 3;
        ctx->armed = SLN_ACT_ESP;
        return;
    }
    if (ch == 'k' || ch == KEY_UP) {
        ctx->esp_which = (ctx->esp_which + 2) % 3;
        ctx->armed = SLN_ACT_ESP;
        return;
    }
    if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
        ctx->armed = SLN_ACT_ESP;
        ctx->pending = SLN_ACT_ESP;
    }
}

const sln_pane sln_pane_esp32 = {
    "esp32",
    "j/k build flash monitor",
    sample,
    draw,
    key
};
