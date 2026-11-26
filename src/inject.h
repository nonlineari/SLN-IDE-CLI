#ifndef SLN_INJECT_H
#define SLN_INJECT_H

/*
 * A pane is one translation unit with sample, draw, and key.
 * main.c lists the pane objects. That table is the whole injection
 * mechanism: the core calls the registered functions. There is no
 * dlopen, no shell evaluation of keystrokes, and no code sent into
 * another process.
 */

#include <ncurses.h>
#include <stddef.h>

enum {
    SLN_NPANES = 9,
    SLN_VIEW_ROWS = 40,
    SLN_VIEW_COLS = 192,
    SLN_PATH_MAX = 512,
    SLN_URL_MAX = 240,
    SLN_ACT_NONE = 0,
    SLN_ACT_ESP = 1,
    SLN_ACT_URL_OPEN = 2,
    SLN_ACT_URL_HEAD = 3
};

typedef struct sln_ctx sln_ctx;

typedef struct sln_pane {
    const char *name;
    const char *hint;
    void (*sample)(sln_ctx *ctx);
    void (*draw)(sln_ctx *ctx, WINDOW *win, int rows, int cols);
    void (*key)(sln_ctx *ctx, int ch);
} sln_pane;

struct sln_ctx {
    int rows;
    int cols;
    int focus;
    int pane_idx;
    int quit;
    int url_edit;
    int esp_which;
    int url_act;
    int armed;
    int pending;
    int have_exit;
    int last_exit;
    int log_seq;
    const sln_pane *panes[SLN_NPANES];
    char selected[SLN_PATH_MAX];
    char url[SLN_URL_MAX];
    char status[192];
    char last_cmd[320];
    char url_note[1200];
    char view[SLN_NPANES][SLN_VIEW_ROWS][SLN_VIEW_COLS];
    int nview[SLN_NPANES];
    int cursor[SLN_NPANES];
    int scroll[SLN_NPANES];
};

extern const sln_pane sln_pane_heatsink;
extern const sln_pane sln_pane_process;
extern const sln_pane sln_pane_security;
extern const sln_pane sln_pane_tree;
extern const sln_pane sln_pane_files;
extern const sln_pane sln_pane_cim;
extern const sln_pane sln_pane_esp32;
extern const sln_pane sln_pane_url;
extern const sln_pane sln_pane_cmdline;

void sln_add(sln_ctx *ctx, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void sln_add_str(sln_ctx *ctx, const char *text);
void sln_add_log_tail(sln_ctx *ctx, const char *path, int max_lines);
void sln_key_scroll(sln_ctx *ctx, int ch);
void sln_draw_view(sln_ctx *ctx, WINDOW *win, int rows, int cols);
void sln_sample_temps(sln_ctx *ctx, const char *prefix, int limit, int with_fan);

int sln_read_str(const char *path, char *buf, size_t n);
int sln_read_int(const char *path, long *out);
int sln_url_char_ok(int ch);
int sln_url_ok(const char *url);
int sln_usb_identity(char *vendor, size_t nv, char *product, size_t np,
                     char *text, size_t nt);

void sln_ui_init(void);
void sln_ui_draw(sln_ctx *ctx);
void sln_ui_end(void);
void sln_run_pending(sln_ctx *ctx);

#endif
