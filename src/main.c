#include "inject.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void bind_panes(sln_ctx *ctx)
{
    ctx->panes[0] = &sln_pane_heatsink;
    ctx->panes[1] = &sln_pane_process;
    ctx->panes[2] = &sln_pane_security;
    ctx->panes[3] = &sln_pane_tree;
    ctx->panes[4] = &sln_pane_files;
    ctx->panes[5] = &sln_pane_cim;
    ctx->panes[6] = &sln_pane_esp32;
    ctx->panes[7] = &sln_pane_url;
    ctx->panes[8] = &sln_pane_cmdline;
}

static void seed(sln_ctx *ctx)
{
    memset(ctx, 0, sizeof *ctx);
    bind_panes(ctx);
    ctx->armed = SLN_ACT_ESP;
    memcpy(ctx->selected, SLN_TREE_ROOT, sizeof SLN_TREE_ROOT);
    memcpy(ctx->url, "http://localhost", sizeof "http://localhost");
    memcpy(ctx->status, "noiseprotocol classic", sizeof "noiseprotocol classic");
}

static void sample_all(sln_ctx *ctx)
{
    int i;

    for (i = 0; i < SLN_NPANES; i++) {
        ctx->pane_idx = i;
        ctx->nview[i] = 0;
        ctx->panes[i]->sample(ctx);
    }
}

static int view_has(const sln_ctx *ctx, int pane, const char *needle)
{
    int i;

    for (i = 0; i < ctx->nview[pane]; i++) {
        if (strstr(ctx->view[pane][i], needle)) {
            return 1;
        }
    }
    return 0;
}

static int run_sample(sln_ctx *ctx)
{
    int i;
    int pane;
    int bad = 0;

    sample_all(ctx);
    usleep(200000);
    sample_all(ctx);

    if (!sln_url_ok("http://localhost") || !sln_url_ok("https://localhost/a")) {
        fprintf(stderr, "url-check: accept failed\n");
        bad = 1;
    }
    if (sln_url_ok("file:///etc/passwd") || sln_url_ok("https://") ||
        sln_url_ok("http://x;rm") || sln_url_ok("https://a b") ||
        sln_url_ok("https://localhost/$(x)")) {
        fprintf(stderr, "url-check: reject failed\n");
        bad = 1;
    }
    if (!bad) {
        printf("url-check: pass\n");
    }

    for (pane = 0; pane < SLN_NPANES; pane++) {
        printf("== %s\n", ctx->panes[pane]->name);
        for (i = 0; i < ctx->nview[pane]; i++) {
            printf("%s\n", ctx->view[pane][i]);
        }
    }

    if (!view_has(ctx, 0, "CPU") || !view_has(ctx, 0, "Fan")) {
        fprintf(stderr, "sample-check: heatsink\n");
        bad = 1;
    }
    if (!view_has(ctx, 1, "load ")) {
        fprintf(stderr, "sample-check: process\n");
        bad = 1;
    }
    if (!view_has(ctx, 2, "uid ") || !view_has(ctx, 2, "ttyUSB0")) {
        fprintf(stderr, "sample-check: security\n");
        bad = 1;
    }
    if (ctx->nview[3] < 1) {
        fprintf(stderr, "sample-check: tree\n");
        bad = 1;
    }
    if (!view_has(ctx, 4, SLN_TREE_ROOT)) {
        fprintf(stderr, "sample-check: files\n");
        bad = 1;
    }
    if (!view_has(ctx, 5, "ComputerSystem") || !view_has(ctx, 5, "ESP32Board") ||
        !view_has(ctx, 5, "product ")) {
        fprintf(stderr, "sample-check: cim\n");
        bad = 1;
    }
    if (!view_has(ctx, 6, "noiseprotocol_classic.bin") || !view_has(ctx, 6, "* build")) {
        fprintf(stderr, "sample-check: esp32\n");
        bad = 1;
    }
    if (!view_has(ctx, 7, "http://localhost")) {
        fprintf(stderr, "sample-check: url\n");
        bad = 1;
    }
    if (bad) {
        fprintf(stderr, "sample-check: fail\n");
        return 1;
    }
    printf("sample-check: pass\n");
    return 0;
}

static int run_act(sln_ctx *ctx, const char *which)
{
    if (strcmp(which, "url-head") == 0) {
        ctx->url_act = 1;
        ctx->pending = SLN_ACT_URL_HEAD;
    } else if (strcmp(which, "url-open") == 0) {
        ctx->url_act = 0;
        ctx->pending = SLN_ACT_URL_OPEN;
    } else {
        fprintf(stderr, "usage: sln-ide --act url-head|url-open\n");
        return 2;
    }
    sln_run_pending(ctx);
    if (ctx->url_note[0]) {
        fputs(ctx->url_note, stdout);
        if (ctx->url_note[strlen(ctx->url_note) - 1] != '\n') {
            fputc('\n', stdout);
        }
    }
    printf("exit %d\n", ctx->last_exit);
    return ctx->last_exit == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    static sln_ctx ctx;

    seed(&ctx);
    if (argc == 2 && strcmp(argv[1], "--sample") == 0) {
        return run_sample(&ctx);
    }
    if (argc == 3 && strcmp(argv[1], "--act") == 0) {
        return run_act(&ctx, argv[2]);
    }
    if (argc != 1) {
        fprintf(stderr, "usage: sln-ide [--sample]\n");
        return 2;
    }

    sln_ui_init();
    while (!ctx.quit) {
        int ch;

        sample_all(&ctx);
        sln_ui_draw(&ctx);
        ch = getch();
        if (ch == ERR || ch == KEY_RESIZE) {
            continue;
        }
        if (ch == '\t' || ch == KEY_BTAB) {
            ctx.url_edit = 0;
            if (ch == '\t') {
                ctx.focus = (ctx.focus + 1) % SLN_NPANES;
            } else {
                ctx.focus = (ctx.focus + SLN_NPANES - 1) % SLN_NPANES;
            }
            continue;
        }
        if (ch == 'q' && !ctx.url_edit) {
            ctx.quit = 1;
            continue;
        }
        ctx.pane_idx = ctx.focus;
        ctx.panes[ctx.focus]->key(&ctx, ch);
        if (ctx.pending != SLN_ACT_NONE) {
            sln_run_pending(&ctx);
        }
    }
    sln_ui_end();
    return 0;
}
