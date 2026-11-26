#include "inject.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static void copy_text(char *dst, size_t n, const char *src)
{
    size_t i = 0;

    if (n == 0) {
        return;
    }
    if (src) {
        while (src[i] && i + 1 < n) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

void sln_add_str(sln_ctx *ctx, const char *text)
{
    int pane;
    char *dst;

    pane = ctx->pane_idx;
    if (pane < 0 || pane >= SLN_NPANES) {
        return;
    }
    if (ctx->nview[pane] >= SLN_VIEW_ROWS) {
        return;
    }
    dst = ctx->view[pane][ctx->nview[pane]];
    copy_text(dst, SLN_VIEW_COLS, text);
    ctx->nview[pane]++;
}

void sln_add(sln_ctx *ctx, const char *fmt, ...)
{
    char tmp[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    sln_add_str(ctx, tmp);
}

int sln_read_str(const char *path, char *buf, size_t n)
{
    FILE *f;
    size_t len;

    if (!buf || n < 2) {
        return -1;
    }
    buf[0] = 0;
    f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    if (!fgets(buf, (int)n, f)) {
        fclose(f);
        buf[0] = 0;
        return -1;
    }
    fclose(f);
    len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = 0;
    }
    return 0;
}

int sln_read_int(const char *path, long *out)
{
    char buf[64];
    char *end = NULL;
    long v;

    if (sln_read_str(path, buf, sizeof buf) != 0) {
        return -1;
    }
    errno = 0;
    v = strtol(buf, &end, 10);
    if (end == buf || errno != 0) {
        return -1;
    }
    *out = v;
    return 0;
}

static int read_tail(const char *path, char *buf, size_t n)
{
    FILE *f;
    long sz;
    long start;
    size_t got;
    char *nl;

    if (n < 2) {
        return -1;
    }
    f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    start = 0;
    if ((size_t)sz + 1 > n) {
        start = sz - (long)(n - 1);
    }
    if (fseek(f, start, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    got = fread(buf, 1, n - 1, f);
    fclose(f);
    buf[got] = 0;
    if (start > 0) {
        nl = strchr(buf, '\n');
        if (nl && nl[1]) {
            memmove(buf, nl + 1, strlen(nl + 1) + 1);
        }
    }
    return 0;
}

void sln_add_log_tail(sln_ctx *ctx, const char *path, int max_lines)
{
    char chunk[8192];
    char *lines[80];
    int n = 0;
    int start;
    int i;
    char *p;

    if (max_lines < 1) {
        return;
    }
    if (max_lines > 80) {
        max_lines = 80;
    }
    if (read_tail(path, chunk, sizeof chunk) != 0) {
        sln_add_str(ctx, "(no log yet)");
        return;
    }
    p = chunk;
    while (*p && n < 80) {
        lines[n++] = p;
        p = strchr(p, '\n');
        if (!p) {
            break;
        }
        *p = 0;
        p++;
    }
    while (n > 0 && lines[n - 1][0] == 0) {
        n--;
    }
    if (n == 0) {
        sln_add_str(ctx, "(no log yet)");
        return;
    }
    start = n > max_lines ? n - max_lines : 0;
    for (i = start; i < n; i++) {
        size_t k;
        for (k = 0; lines[i][k]; k++) {
            unsigned char c = (unsigned char)lines[i][k];
            if (c < 32 || c == 127) {
                lines[i][k] = ' ';
            }
        }
        sln_add_str(ctx, lines[i]);
    }
}

void sln_key_scroll(sln_ctx *ctx, int ch)
{
    int pane = ctx->pane_idx;
    int n = ctx->nview[pane];
    int c = ctx->cursor[pane];

    if (ch == 'j' || ch == KEY_DOWN) {
        if (c + 1 < n) {
            c++;
        }
    } else if (ch == 'k' || ch == KEY_UP) {
        if (c > 0) {
            c--;
        }
    }
    ctx->cursor[pane] = c;
}

static void put_line(WINDOW *win, int row, int cols, int attr, const char *text)
{
    char buf[512];
    int n;
    int i;

    if (row < 0 || cols <= 0) {
        return;
    }
    n = cols;
    if (n > (int)sizeof buf - 1) {
        n = (int)sizeof buf - 1;
    }
    for (i = 0; i < n; i++) {
        buf[i] = ' ';
    }
    buf[n] = 0;
    if (text) {
        for (i = 0; text[i] && i < n; i++) {
            unsigned char c = (unsigned char)text[i];
            buf[i] = (c < 32 || c == 127) ? ' ' : (char)c;
        }
    }
    if (attr) {
        wattron(win, attr);
    }
    mvwaddnstr(win, row + 1, 1, buf, n);
    if (attr) {
        wattroff(win, attr);
    }
}

void sln_draw_view(sln_ctx *ctx, WINDOW *win, int rows, int cols)
{
    int pane = ctx->pane_idx;
    int n = ctx->nview[pane];
    int c = ctx->cursor[pane];
    int off;
    int r;

    if (c >= n) {
        c = n > 0 ? n - 1 : 0;
    }
    if (c < 0) {
        c = 0;
    }
    ctx->cursor[pane] = c;
    off = ctx->scroll[pane];
    if (rows < 1) {
        return;
    }
    if (c < off) {
        off = c;
    }
    if (c >= off + rows) {
        off = c - rows + 1;
    }
    if (off < 0) {
        off = 0;
    }
    ctx->scroll[pane] = off;
    for (r = 0; r < rows && r + off < n; r++) {
        int idx = r + off;
        int attr = (idx == c && pane == ctx->focus) ? A_REVERSE : 0;
        put_line(win, r, cols, attr, ctx->view[pane][idx]);
    }
}

static void copy_token(char *dst, size_t n, const char *src)
{
    size_t i = 0;

    if (n == 0) {
        return;
    }
    while (src[i] && i + 1 < n && i < 24) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void add_temp_line(sln_ctx *ctx, const char *prefix, const char *who,
                          const char *label, int have_value, long mc)
{
    long mag;
    long whole;
    long frac;
    const char *sign;

    if (!have_value) {
        if (label[0]) {
            sln_add(ctx, "%s%s %s", prefix, who, label);
        }
        return;
    }
    sign = mc < 0 ? "-" : "";
    mag = mc < 0 ? -mc : mc;
    whole = mag / 1000;
    frac = (mag % 1000) / 100;
    if (label[0]) {
        sln_add(ctx, "%s%s %s  %s%ld.%ld C", prefix, who, label, sign, whole, frac);
    } else {
        sln_add(ctx, "%s%s  %s%ld.%ld C", prefix, who, sign, whole, frac);
    }
}

void sln_sample_temps(sln_ctx *ctx, const char *prefix, int limit, int with_fan)
{
    DIR *dir;
    struct dirent *de;
    char seen[24][32];
    int nseen = 0;
    int shown = 0;
    int i;

    if (!prefix) {
        prefix = "";
    }
    if (with_fan) {
        for (i = 0; i < 16; i++) {
            char p[80];
            char type[32];
            long cur = 0;
            long max = 0;

            snprintf(p, sizeof p, "/sys/class/thermal/cooling_device%d/type", i);
            if (sln_read_str(p, type, sizeof type) != 0) {
                continue;
            }
            if (strcasecmp(type, "fan") != 0) {
                continue;
            }
            snprintf(p, sizeof p, "/sys/class/thermal/cooling_device%d/cur_state", i);
            if (sln_read_int(p, &cur) != 0) {
                continue;
            }
            snprintf(p, sizeof p, "/sys/class/thermal/cooling_device%d/max_state", i);
            if (sln_read_int(p, &max) != 0) {
                continue;
            }
            sln_add(ctx, "%sFan cooling_device%d  %ld/%ld", prefix, i, cur, max);
        }
    }
    dir = opendir("/sys/class/hwmon");
    if (dir) {
        while ((de = readdir(dir)) != NULL) {
            char name[16];
            char who[32];
            char base[64];
            int t;

            if (strncmp(de->d_name, "hwmon", 5) != 0) {
                continue;
            }
            copy_token(name, sizeof name, de->d_name);
            snprintf(base, sizeof base, "/sys/class/hwmon/%s", name);
            snprintf(who, sizeof who, "%s", name);
            {
                char p[96];
                char raw[64];
                snprintf(p, sizeof p, "%s/name", base);
                if (sln_read_str(p, raw, sizeof raw) == 0 && raw[0]) {
                    copy_token(who, sizeof who, raw);
                }
            }
            if (nseen < 24) {
                copy_text(seen[nseen], sizeof seen[nseen], who);
                nseen++;
            }
            for (t = 1; t <= 12; t++) {
                char p[96];
                char label[48];
                long mc = 0;
                int have = 0;

                if (limit > 0 && shown >= limit) {
                    break;
                }
                label[0] = 0;
                snprintf(p, sizeof p, "%s/temp%d_label", base, t);
                if (sln_read_str(p, label, sizeof label) != 0) {
                    label[0] = 0;
                }
                snprintf(p, sizeof p, "%s/temp%d_input", base, t);
                if (sln_read_int(p, &mc) == 0) {
                    have = 1;
                }
                if (!have && label[0] == 0) {
                    continue;
                }
                add_temp_line(ctx, prefix, who, label, have, mc);
                shown++;
            }
        }
        closedir(dir);
    }

    for (i = 0; i < 8; i++) {
        char p[80];
        char type[48];
        long mc = 0;
        int known = 0;
        int s;

        if (limit > 0 && shown >= limit) {
            break;
        }
        snprintf(p, sizeof p, "/sys/class/thermal/thermal_zone%d/type", i);
        if (sln_read_str(p, type, sizeof type) != 0) {
            continue;
        }
        for (s = 0; s < nseen; s++) {
            if (strcmp(seen[s], type) == 0) {
                known = 1;
                break;
            }
        }
        if (known) {
            continue;
        }
        snprintf(p, sizeof p, "/sys/class/thermal/thermal_zone%d/temp", i);
        if (sln_read_int(p, &mc) != 0) {
            add_temp_line(ctx, prefix, type, "", 0, 0);
        } else {
            add_temp_line(ctx, prefix, type, "", 1, mc);
        }
        shown++;
    }

}

int sln_usb_identity(char *vendor, size_t nv, char *product, size_t np,
                     char *text, size_t nt)
{
    char path[PATH_MAX];
    int hop;

    if (vendor && nv) {
        vendor[0] = 0;
    }
    if (product && np) {
        product[0] = 0;
    }
    if (text && nt) {
        text[0] = 0;
    }
    if (!vendor || nv < 2 || !product || np < 2) {
        return -1;
    }
    if (!realpath("/sys/class/tty/ttyUSB0/device", path)) {
        return -1;
    }
    for (hop = 0; hop < 12; hop++) {
        char file[PATH_MAX + 16];
        size_t lp = strlen(path);
        char *slash;

        if (lp + 12 >= sizeof file) {
            break;
        }
        memcpy(file, path, lp);
        memcpy(file + lp, "/idVendor", 10);
        if (sln_read_str(file, vendor, nv) == 0) {
            memcpy(file + lp, "/idProduct", 11);
            sln_read_str(file, product, np);
            if (text && nt) {
                memcpy(file + lp, "/product", 9);
                if (sln_read_str(file, text, nt) != 0) {
                    text[0] = 0;
                }
            }
            return 0;
        }
        slash = strrchr(path, '/');
        if (!slash || slash == path) {
            break;
        }
        *slash = 0;
    }
    return -1;
}

int sln_url_char_ok(int ch)
{
    unsigned char c = (unsigned char)ch;

    if (c < 0x20 || c == 0x7f || c == ' ' || c == '`') {
        return 0;
    }
    if (c == '\'' || c == '"' || c == '\\' || c == '|' || c == ';' || c == '$' ||
        c == '&' || c == '<' || c == '>' || c == '(' || c == ')' || c == '{' ||
        c == '}') {
        return 0;
    }
    return 1;
}

int sln_url_ok(const char *url)
{
    const char *rest;
    size_t n;
    size_t i;

    if (!url) {
        return 0;
    }
    n = strlen(url);
    if (n == 0 || n >= SLN_URL_MAX) {
        return 0;
    }
    if (strncmp(url, "https://", 8) == 0) {
        rest = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        rest = url + 7;
    } else {
        return 0;
    }
    if (*rest == '\0' || *rest == '/') {
        return 0;
    }
    for (i = 0; url[i]; i++) {
        if (!sln_url_char_ok((unsigned char)url[i])) {
            return 0;
        }
    }
    return 1;
}

static void set_status(sln_ctx *ctx, const char *text)
{
    copy_text(ctx->status, sizeof ctx->status, text);
}

static void capture_append(char *out, size_t n, size_t *used, const char *tmp, size_t got)
{
    size_t take;

    if (!out || *used + 1 >= n) {
        return;
    }
    take = got;
    if (take > n - 1 - *used) {
        take = n - 1 - *used;
    }
    memcpy(out + *used, tmp, take);
    *used += take;
    out[*used] = 0;
}

static int run_capture(char *const argv[], char *out, size_t n, int *status)
{
    int pipefd[2];
    pid_t pid;
    int st = 0;
    int reaped = 0;
    size_t used = 0;
    int idle = 0;

    if (out && n) {
        out[0] = 0;
    }
    if (pipe(pipefd) < 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    while (!reaped && idle < 16) {
        struct pollfd pfd;
        char tmp[512];
        ssize_t r;
        int pr;
        int wr;

        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        pr = poll(&pfd, 1, 500);
        if (pr > 0) {
            idle = 0;
            for (;;) {
                r = read(pipefd[0], tmp, sizeof tmp);
                if (r < 0) {
                    if (errno == EINTR || errno == EAGAIN) {
                        break;
                    }
                    break;
                }
                if (r == 0) {
                    break;
                }
                capture_append(out, n, &used, tmp, (size_t)r);
            }
        } else if (pr == 0) {
            idle++;
        }
        wr = waitpid(pid, &st, WNOHANG);
        if (wr == pid) {
            reaped = 1;
        } else if (wr < 0 && errno != EINTR) {
            reaped = 1;
        }
    }
    if (!reaped) {
        kill(pid, SIGKILL);
        waitpid(pid, &st, 0);
    } else {
        char tmp[512];
        ssize_t r;
        while ((r = read(pipefd[0], tmp, sizeof tmp)) > 0) {
            capture_append(out, n, &used, tmp, (size_t)r);
        }
    }
    close(pipefd[0]);
    if (status) {
        if (st >= 0 && WIFEXITED(st)) {
            *status = WEXITSTATUS(st);
        } else {
            *status = -1;
        }
    }
    return 0;
}

static int run_fg(char *const argv[], int *status)
{
    struct termios saved;
    sigset_t block;
    sigset_t oldset;
    pid_t pid;
    int st = 0;
    int have_tty;
    int code = -1;

    fflush(NULL);
    def_prog_mode();
    endwin();
    have_tty = isatty(STDIN_FILENO);
    if (have_tty) {
        tcgetattr(STDIN_FILENO, &saved);
    }
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGQUIT);
    sigaddset(&block, SIGTSTP);
    sigprocmask(SIG_BLOCK, &block, &oldset);

    pid = fork();
    if (pid < 0) {
        sigprocmask(SIG_SETMASK, &oldset, NULL);
        reset_prog_mode();
        refresh();
        return -1;
    }
    if (pid == 0) {
        if (have_tty) {
            setpgid(0, 0);
            tcsetpgrp(STDIN_FILENO, getpid());
        }
        sigprocmask(SIG_SETMASK, &oldset, NULL);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGWINCH, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        execvp(argv[0], argv);
        fprintf(stderr, "cannot exec %s\n", argv[0]);
        _exit(127);
    }
    if (have_tty) {
        setpgid(pid, pid);
        tcsetpgrp(STDIN_FILENO, pid);
    }
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) {
            st = -1;
            break;
        }
    }
    if (have_tty) {
        tcsetpgrp(STDIN_FILENO, getpgrp());
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
    sigprocmask(SIG_SETMASK, &oldset, NULL);
    if (st >= 0 && WIFEXITED(st)) {
        code = WEXITSTATUS(st);
    }
    if (status) {
        *status = code;
    }
    if (have_tty) {
        char line[8];
        fprintf(stdout, "\n[sln-ide] exit %d\nlog %s\npress enter to return to the panels\n",
                code, SLN_ESP_LOG);
        fflush(stdout);
        if (!fgets(line, (int)sizeof line, stdin)) {
            clearerr(stdin);
        }
    }
    reset_prog_mode();
    refresh();
    return 0;
}

static const char *esp_name(int which)
{
    if (which == 1) {
        return "flash";
    }
    if (which == 2) {
        return "monitor";
    }
    return "build";
}

static void note_copy(sln_ctx *ctx, const char *text)
{
    copy_text(ctx->url_note, sizeof ctx->url_note, text);
}

static void run_url(sln_ctx *ctx, int open_browser)
{
    char out[1200];
    int st = -1;
    char *argv_open[3];
    char *argv_head[9];

    if (!sln_url_ok(ctx->url)) {
        set_status(ctx, "url needs one http:// or https:// address");
        note_copy(ctx, "rejected: scheme must be http or https, host nonempty");
        ctx->have_exit = 1;
        ctx->last_exit = 1;
        copy_text(ctx->last_cmd, sizeof ctx->last_cmd, "url rejected");
        return;
    }
    out[0] = 0;
    if (open_browser) {
        argv_open[0] = "xdg-open";
        argv_open[1] = ctx->url;
        argv_open[2] = NULL;
        snprintf(ctx->last_cmd, sizeof ctx->last_cmd, "xdg-open %s", ctx->url);
        run_capture(argv_open, out, sizeof out, &st);
    } else {
        argv_head[0] = "curl";
        argv_head[1] = "-I";
        argv_head[2] = "-sS";
        argv_head[3] = "--max-time";
        argv_head[4] = "5";
        argv_head[5] = "--proto";
        argv_head[6] = "=http,https";
        argv_head[7] = ctx->url;
        argv_head[8] = NULL;
        snprintf(ctx->last_cmd, sizeof ctx->last_cmd, "curl -I %s", ctx->url);
        run_capture(argv_head, out, sizeof out, &st);
    }
    ctx->have_exit = 1;
    ctx->last_exit = st;
    ctx->log_seq++;
    note_copy(ctx, out[0] ? out : "(no output)");
    if (st == 0) {
        set_status(ctx, open_browser ? "browser launched" : "curl finished");
    } else {
        char msg[96];
        snprintf(msg, sizeof msg, "%s exit %d", open_browser ? "xdg-open" : "curl", st);
        set_status(ctx, msg);
    }
}

void sln_run_pending(sln_ctx *ctx)
{
    int act = ctx->pending;
    char *argv[3];
    int st = -1;

    ctx->pending = SLN_ACT_NONE;
    if (act == SLN_ACT_ESP) {
        if (ctx->esp_which < 0 || ctx->esp_which > 2) {
            ctx->esp_which = 0;
        }
        argv[0] = (char *)SLN_ESP_SH;
        argv[1] = (char *)esp_name(ctx->esp_which);
        argv[2] = NULL;
        setenv("SLN_ESP_LOG", SLN_ESP_LOG, 1);
        snprintf(ctx->last_cmd, sizeof ctx->last_cmd, "esp.sh %s", esp_name(ctx->esp_which));
        run_fg(argv, &st);
        ctx->have_exit = 1;
        ctx->last_exit = st;
        ctx->log_seq++;
        {
            char msg[96];
            snprintf(msg, sizeof msg, "esp.sh %s exit %d", esp_name(ctx->esp_which), st);
            set_status(ctx, msg);
        }
        return;
    }
    if (act == SLN_ACT_URL_OPEN || act == SLN_ACT_URL_HEAD) {
        run_url(ctx, act == SLN_ACT_URL_OPEN);
    }
}

void sln_ui_init(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, FALSE);
    timeout(1000);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
    }
}

void sln_ui_end(void)
{
    endwin();
}

void sln_ui_draw(sln_ctx *ctx)
{
    int rows;
    int cols;
    int gh;
    int gw;
    int i;
    WINDOW *wins[SLN_NPANES];

    getmaxyx(stdscr, rows, cols);
    ctx->rows = rows;
    ctx->cols = cols;
    erase();
    for (i = 0; i < SLN_NPANES; i++) {
        wins[i] = NULL;
    }
    if (rows < 16 || cols < 40) {
        mvprintw(0, 0, "SLN-IDE-CLI needs at least 40x16");
        refresh();
        return;
    }
    gh = (rows - 1) / 3;
    gw = cols / 3;
    for (i = 0; i < SLN_NPANES; i++) {
        int r = i / 3;
        int c = i % 3;
        int y = r * gh;
        int x = c * gw;
        int h = (r == 2) ? (rows - 1 - 2 * gh) : gh;
        int w = (c == 2) ? (cols - 2 * gw) : gw;
        WINDOW *win;
        int focus;

        if (h < 3 || w < 8) {
            continue;
        }
        win = newwin(h, w, y, x);
        if (!win) {
            continue;
        }
        wins[i] = win;
        focus = (i == ctx->focus);
        if (focus && has_colors()) {
            wattron(win, COLOR_PAIR(1) | A_BOLD);
        } else if (focus) {
            wattron(win, A_BOLD);
        }
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " %s ", ctx->panes[i]->name);
        if (focus && has_colors()) {
            wattroff(win, COLOR_PAIR(1) | A_BOLD);
        } else if (focus) {
            wattroff(win, A_BOLD);
        }
        ctx->pane_idx = i;
        ctx->panes[i]->draw(ctx, win, h - 2, w - 2);
    }
    {
        char bar[512];
        const char *hint = ctx->panes[ctx->focus]->hint;
        bar[0] = 0;
        copy_text(bar, sizeof bar, " ");
        {
            size_t u = strlen(bar);
            const char *bits[5];
            int b;
            bits[0] = ctx->panes[ctx->focus]->name;
            bits[1] = " | ";
            bits[2] = hint ? hint : "";
            bits[3] = " | tab j/k enter q | ";
            bits[4] = ctx->status;
            for (b = 0; b < 5; b++) {
                size_t k = 0;
                while (bits[b][k] && u + 1 < sizeof bar) {
                    bar[u++] = bits[b][k++];
                }
                bar[u] = 0;
            }
        }
        attron(A_REVERSE);
        mvhline(rows - 1, 0, ' ', cols);
        mvaddnstr(rows - 1, 0, bar, cols > 0 ? cols - 1 : 0);
        attroff(A_REVERSE);
    }
    /* stdscr is mostly blank. Copy it first so the panes land on top. */
    wnoutrefresh(stdscr);
    for (i = 0; i < SLN_NPANES; i++) {
        if (wins[i]) {
            wnoutrefresh(wins[i]);
        }
    }
    doupdate();
    for (i = 0; i < SLN_NPANES; i++) {
        if (wins[i]) {
            delwin(wins[i]);
        }
    }
}
