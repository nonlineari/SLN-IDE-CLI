#include "inject.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { SLN_TREE_MAX = 400, SLN_TREE_DEPTH = 4, SLN_EXP_MAX = 64 };

typedef struct {
    char path[SLN_PATH_MAX];
    int depth;
    int parent;
    int is_dir;
    int expanded;
} sln_node;

static sln_node nodes[SLN_TREE_MAX];
static int nnodes;
static char exp_paths[SLN_EXP_MAX][SLN_PATH_MAX];
static int nexp;
static char cursor_path[SLN_PATH_MAX];
static int tree_ready;

static int path_fit(const char *path)
{
    size_t n = strlen(path);
    return n > 0 && n < SLN_PATH_MAX;
}

static int is_expanded(const char *path)
{
    int i;

    for (i = 0; i < nexp; i++) {
        if (strcmp(exp_paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

static void exp_set(const char *path, int on)
{
    int i;

    if (!path_fit(path)) {
        return;
    }
    for (i = 0; i < nexp; i++) {
        if (strcmp(exp_paths[i], path) == 0) {
            break;
        }
    }
    if (on) {
        if (i < nexp || nexp >= SLN_EXP_MAX) {
            return;
        }
        memcpy(exp_paths[nexp], path, strlen(path) + 1);
        nexp++;
        return;
    }
    if (i >= nexp) {
        return;
    }
    if (i + 1 < nexp) {
        memmove(exp_paths[i], exp_paths[i + 1],
                (size_t)(nexp - i - 1) * sizeof exp_paths[0]);
    }
    nexp--;
}

static int append_node(const char *path, int depth, int parent, int is_dir)
{
    sln_node *node;

    if (nnodes >= SLN_TREE_MAX || !path_fit(path)) {
        return -1;
    }
    node = &nodes[nnodes];
    memcpy(node->path, path, strlen(path) + 1);
    node->depth = depth;
    node->parent = parent;
    node->is_dir = is_dir;
    node->expanded = is_dir && is_expanded(path);
    nnodes++;
    return nnodes - 1;
}

static int cmp_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static void scan_children(int parent)
{
    DIR *dir;
    struct dirent *de;
    char (*names)[256];
    int kids[256];
    int nc = 0;
    int nk = 0;
    int i;

    if (parent < 0 || parent >= nnodes) {
        return;
    }
    if (nodes[parent].depth >= SLN_TREE_DEPTH || !nodes[parent].expanded) {
        return;
    }
    dir = opendir(nodes[parent].path);
    if (!dir) {
        return;
    }
    names = calloc(256, 256);
    if (!names) {
        closedir(dir);
        return;
    }
    while ((de = readdir(dir)) != NULL && nc < 256) {
        size_t nlen;
        if (de->d_name[0] == '.') {
            continue;
        }
        nlen = strlen(de->d_name);
        if (nlen == 0 || nlen >= 256) {
            continue;
        }
        memcpy(names[nc], de->d_name, nlen + 1);
        nc++;
    }
    closedir(dir);
    qsort(names, (size_t)nc, 256, cmp_names);
    for (i = 0; i < nc && nnodes < SLN_TREE_MAX; i++) {
        char child[SLN_PATH_MAX];
        size_t pl = strlen(nodes[parent].path);
        size_t nl = strlen(names[i]);
        struct stat st;
        int is_dir;
        int idx;

        if (pl + 1 + nl >= SLN_PATH_MAX) {
            continue;
        }
        memcpy(child, nodes[parent].path, pl);
        child[pl] = '/';
        memcpy(child + pl + 1, names[i], nl + 1);
        if (lstat(child, &st) != 0 || S_ISLNK(st.st_mode)) {
            continue;
        }
        is_dir = S_ISDIR(st.st_mode);
        if (is_dir && strcmp(names[i], "build") == 0) {
            continue;
        }
        idx = append_node(child, nodes[parent].depth + 1, parent, is_dir);
        if (idx >= 0 && nk < 256) {
            kids[nk++] = idx;
        }
    }
    free(names);
    for (i = 0; i < nk; i++) {
        if (nodes[kids[i]].is_dir && nodes[kids[i]].expanded) {
            scan_children(kids[i]);
        }
    }
}

static void add_node_line(sln_ctx *ctx, int idx)
{
    char line[SLN_VIEW_COLS];
    size_t u = 0;
    int depth;
    const char *base;

    for (depth = 0; depth < nodes[idx].depth && u + 2 < sizeof line; depth++) {
        line[u++] = ' ';
        line[u++] = ' ';
    }
    if (u + 2 < sizeof line) {
        if (nodes[idx].is_dir) {
            line[u++] = nodes[idx].expanded ? '-' : '+';
        } else {
            line[u++] = ' ';
        }
        line[u++] = ' ';
    }
    base = strrchr(nodes[idx].path, '/');
    base = (base && base[1]) ? base + 1 : nodes[idx].path;
    while (*base && u + 1 < sizeof line) {
        line[u++] = *base++;
    }
    line[u] = 0;
    sln_add_str(ctx, line);
}

static void remember_cursor(sln_ctx *ctx, int idx)
{
    if (idx < 0 || idx >= nnodes) {
        return;
    }
    memcpy(cursor_path, nodes[idx].path, strlen(nodes[idx].path) + 1);
    memcpy(ctx->selected, nodes[idx].path, strlen(nodes[idx].path) + 1);
    ctx->cursor[ctx->pane_idx] = idx;
}

static void sample(sln_ctx *ctx)
{
    int i;
    int found = 0;
    struct stat st;

    if (!tree_ready) {
        exp_set(SLN_TREE_ROOT, 1);
        memcpy(cursor_path, SLN_TREE_ROOT, sizeof SLN_TREE_ROOT);
        tree_ready = 1;
    }
    nnodes = 0;
    if (lstat(SLN_TREE_ROOT, &st) != 0 || !S_ISDIR(st.st_mode)) {
        sln_add_str(ctx, "missing " SLN_TREE_ROOT);
        return;
    }
    if (append_node(SLN_TREE_ROOT, 0, -1, 1) < 0) {
        sln_add_str(ctx, "tree empty");
        return;
    }
    scan_children(0);
    for (i = 0; i < nnodes; i++) {
        add_node_line(ctx, i);
        if (!found && strcmp(nodes[i].path, cursor_path) == 0) {
            ctx->cursor[ctx->pane_idx] = i;
            memcpy(ctx->selected, nodes[i].path, strlen(nodes[i].path) + 1);
            found = 1;
        }
    }
    if (!found && nnodes > 0) {
        remember_cursor(ctx, 0);
    }
    if (nnodes >= SLN_TREE_MAX) {
        sln_add_str(ctx, "(tree truncated)");
    }
}

static void draw(sln_ctx *ctx, WINDOW *win, int rows, int cols)
{
    sln_draw_view(ctx, win, rows, cols);
}

static void key(sln_ctx *ctx, int ch)
{
    int pane = ctx->pane_idx;
    int c = ctx->cursor[pane];

    if (nnodes <= 0) {
        return;
    }
    if (c < 0 || c >= nnodes) {
        c = 0;
    }
    if (ch == 'j' || ch == KEY_DOWN) {
        if (c + 1 < nnodes) {
            c++;
        }
        remember_cursor(ctx, c);
        return;
    }
    if (ch == 'k' || ch == KEY_UP) {
        if (c > 0) {
            c--;
        }
        remember_cursor(ctx, c);
        return;
    }
    if (ch == 'h' || ch == KEY_LEFT) {
        if (nodes[c].is_dir && nodes[c].expanded) {
            exp_set(nodes[c].path, 0);
        } else if (nodes[c].parent >= 0) {
            c = nodes[c].parent;
            remember_cursor(ctx, c);
        }
        return;
    }
    if (ch == 'l' || ch == KEY_RIGHT || ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
        if (nodes[c].is_dir) {
            exp_set(nodes[c].path, 1);
        }
        remember_cursor(ctx, c);
    }
}

const sln_pane sln_pane_tree = {
    "tree",
    "h/l fold  j/k move",
    sample,
    draw,
    key
};
