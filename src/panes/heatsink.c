#include "inject.h"

static void sample(sln_ctx *ctx)
{
    sln_sample_temps(ctx, "", 0, 1);
    if (ctx->nview[ctx->pane_idx] == 0) {
        sln_add_str(ctx, "(no sensors)");
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

const sln_pane sln_pane_heatsink = {
    "heatsink",
    "temperatures and fan",
    sample,
    draw,
    key
};
