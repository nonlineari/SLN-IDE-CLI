#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <ncurses.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

enum { GRID = 9, CELL_ROWS = 10, CELL_COLS = 96, CAP_ROWS = 8, ACTS = 2 };

static const char *const k_title[GRID] = {
    "board", "serial", "volume", "disk", "uf2", "files", "actions", "result", "log"
};
static const char *const k_act[ACTS] = {"info", "volume"};

static char g_cell[GRID][CELL_ROWS][CELL_COLS];
static int g_ncell[GRID];
static int g_focus = 6;
static int g_which;
static int g_exit = -1;
static char g_cap[CAP_ROWS][CELL_COLS];
static int g_ncap;
static volatile sig_atomic_t g_stop;

static void on_stop(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void cell_add(int pane, const char *text)
{
    size_t i = 0;

    if (pane < 0 || pane >= GRID || g_ncell[pane] >= CELL_ROWS) {
        return;
    }
    if (text) {
        while (text[i] && i + 1 < CELL_COLS) {
            g_cell[pane][g_ncell[pane]][i] = text[i];
            i++;
        }
    }
    g_cell[pane][g_ncell[pane]][i] = 0;
    g_ncell[pane]++;
}

static void cell_f(int pane, const char *fmt, ...)
{
    char tmp[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    cell_add(pane, tmp);
}

static int read_str(const char *path, char *buf, size_t n)
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

static int is_rp2350(const char *product)
{
    return product && (strstr(product, "RP2350") || strstr(product, "Pico 2") ||
                       strstr(product, "Pico2"));
}

static int find_usb(char *id, size_t nid, char *product, size_t nprod, char *serial,
                    size_t nserial, char *port, size_t nport)
{
    DIR *dir;
    struct dirent *ent;

    id[0] = product[0] = serial[0] = port[0] = 0;
    dir = opendir("/sys/bus/usb/devices");
    if (!dir) {
        return -1;
    }
    while ((ent = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        char vendor[16];
        char prod_id[16];
        char prod[128];
        char ser[64];

        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/idVendor", ent->d_name);
        if (read_str(path, vendor, sizeof vendor) != 0 || strcmp(vendor, "2e8a") != 0) {
            continue;
        }
        snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/product", ent->d_name);
        if (read_str(path, prod, sizeof prod) != 0 || !is_rp2350(prod)) {
            continue;
        }
        snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/idProduct", ent->d_name);
        if (read_str(path, prod_id, sizeof prod_id) != 0) {
            continue;
        }
        snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/serial", ent->d_name);
        if (read_str(path, ser, sizeof ser) != 0) {
            ser[0] = 0;
        }
        snprintf(id, nid, "%s:%s", vendor, prod_id);
        snprintf(product, nprod, "%s", prod);
        snprintf(serial, nserial, "%s", ser);
        snprintf(port, nport, "%s", ent->d_name);
        closedir(dir);
        return 0;
    }
    closedir(dir);
    return -1;
}

static void unescape_mount(char *s)
{
    char *r = s;
    char *w = s;

    while (*r) {
        if (r[0] == '\\' && r[1] == '0' && r[2] == '4' && r[3] == '0') {
            *w++ = ' ';
            r += 4;
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

static int part1_name(const char *name)
{
    size_t n;

    if (strncmp(name, "usb-RPI_RP2350_", 15) != 0) {
        return 0;
    }
    n = strlen(name);
    return n >= 6 && strcmp(name + n - 6, "-part1") == 0;
}

static int find_volume(char *disk, size_t ndisk, char *mount, size_t nmount)
{
    DIR *dir;
    struct dirent *ent;

    disk[0] = mount[0] = 0;
    dir = opendir("/dev/disk/by-id");
    if (!dir) {
        return -1;
    }
    while ((ent = readdir(dir)) != NULL) {
        char link[PATH_MAX];
        char resolved[PATH_MAX];
        FILE *f;
        char row[1024];

        if (!part1_name(ent->d_name)) {
            continue;
        }
        snprintf(link, sizeof link, "/dev/disk/by-id/%s", ent->d_name);
        if (!realpath(link, resolved)) {
            continue;
        }
        snprintf(disk, ndisk, "%s", resolved);
        f = fopen("/proc/mounts", "r");
        if (!f) {
            break;
        }
        while (fgets(row, (int)sizeof row, f)) {
            char dev[512];
            char mnt[512];

            if (sscanf(row, "%511s %511s", dev, mnt) != 2) {
                continue;
            }
            unescape_mount(mnt);
            if (strcmp(dev, resolved) == 0) {
                snprintf(mount, nmount, "%s", mnt);
                break;
            }
        }
        fclose(f);
        closedir(dir);
        return 0;
    }
    closedir(dir);
    return -1;
}

static void add_uf2(const char *mount)
{
    char path[PATH_MAX];
    FILE *f;
    char row[160];
    int n = 0;

    snprintf(path, sizeof path, "%s/INFO_UF2.TXT", mount);
    f = fopen(path, "r");
    if (!f) {
        cell_add(4, "INFO_UF2.TXT absent");
        return;
    }
    while (n < 6 && fgets(row, (int)sizeof row, f)) {
        size_t len = strlen(row);
        while (len > 0 && (row[len - 1] == '\n' || row[len - 1] == '\r')) {
            row[--len] = 0;
        }
        if (row[0]) {
            cell_add(4, row);
            n++;
        }
    }
    fclose(f);
}

static void add_files(const char *mount)
{
    DIR *dir;
    struct dirent *ent;
    int n = 0;

    dir = opendir(mount);
    if (!dir) {
        cell_add(5, "unreadable");
        return;
    }
    while ((ent = readdir(dir)) != NULL && n < 8) {
        char path[PATH_MAX];
        struct stat st;

        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", mount, ent->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        cell_add(5, ent->d_name);
        n++;
    }
    closedir(dir);
    if (n == 0) {
        cell_add(5, "(empty)");
    }
}

static void add_log(void)
{
    FILE *f;
    char kept[6][CELL_COLS];
    char row[256];
    int count = 0;
    int i;
    int start;

    f = fopen(SLN_PICO_LOG, "r");
    if (!f) {
        cell_add(8, "(none)");
        return;
    }
    while (fgets(row, (int)sizeof row, f)) {
        size_t len = strlen(row);
        size_t k = 0;
        while (len > 0 && (row[len - 1] == '\n' || row[len - 1] == '\r')) {
            row[--len] = 0;
        }
        while (row[k] && k + 1 < CELL_COLS) {
            kept[count % 6][k] = row[k];
            k++;
        }
        kept[count % 6][k] = 0;
        count++;
    }
    fclose(f);
    if (count == 0) {
        cell_add(8, "(empty)");
        return;
    }
    start = count > 6 ? count - 6 : 0;
    for (i = start; i < count; i++) {
        cell_add(8, kept[i % 6]);
    }
}

static void sample(void)
{
    char id[32];
    char product[128];
    char serial[64];
    char port[64];
    char disk[PATH_MAX];
    char mount[PATH_MAX];
    int i;

    for (i = 0; i < GRID; i++) {
        g_ncell[i] = 0;
    }
    if (find_usb(id, sizeof id, product, sizeof product, serial, sizeof serial, port,
                 sizeof port) != 0) {
        cell_add(0, "board absent");
        cell_add(1, "serial absent");
    } else {
        cell_f(0, "%s", id);
        cell_add(0, product);
        cell_f(1, "%s", serial[0] ? serial : "unknown");
        cell_f(1, "port %s", port);
    }
    if (find_volume(disk, sizeof disk, mount, sizeof mount) != 0) {
        cell_add(2, "unmounted");
        cell_add(3, "no disk");
        cell_add(5, "(none)");
    } else {
        cell_add(2, mount[0] ? mount : "unmounted");
        cell_add(3, disk[0] ? disk : "no disk");
        if (mount[0]) {
            add_uf2(mount);
            add_files(mount);
        } else {
            cell_add(5, "(none)");
        }
    }
    if (g_ncell[4] == 0) {
        cell_add(4, "no uf2 text");
    }
    for (i = 0; i < ACTS; i++) {
        cell_f(6, "%c %s", i == g_which ? '*' : ' ', k_act[i]);
    }
    if (g_exit < 0) {
        cell_add(7, "(none)");
    } else {
        cell_f(7, "exit %d  pico.sh %s", g_exit, k_act[g_which]);
        for (i = 0; i < g_ncap; i++) {
            cell_add(7, g_cap[i]);
        }
    }
    add_log();
}

static void take_line(char *dst, const char *src, size_t n)
{
    size_t i = 0;

    if (n == 0) {
        return;
    }
    while (src[i] && src[i] != '\n' && src[i] != '\r' && i + 1 < n) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void store_capture(const char *text)
{
    const char *p = text;
    g_ncap = 0;
    while (*p && g_ncap < CAP_ROWS) {
        take_line(g_cap[g_ncap], p, CELL_COLS);
        if (g_cap[g_ncap][0]) {
            g_ncap++;
        }
        while (*p && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
}

static int run_act(void)
{
    int fds[2];
    pid_t pid;
    int st = 0;
    char acc[4096];
    size_t used = 0;
    char *argv[3];

    argv[0] = (char *)SLN_PICO_SH;
    argv[1] = (char *)k_act[g_which];
    argv[2] = NULL;
    acc[0] = 0;
    if (pipe(fds) < 0) {
        g_exit = -1;
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        g_exit = -1;
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > 2) {
                close(devnull);
            }
        }
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        if (fds[0] != STDOUT_FILENO && fds[0] != STDERR_FILENO) {
            close(fds[0]);
        }
        if (fds[1] != STDOUT_FILENO && fds[1] != STDERR_FILENO) {
            close(fds[1]);
        }
        execv(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    for (;;) {
        struct pollfd pfd;
        char tmp[512];
        ssize_t n;
        pid_t got;

        pfd.fd = fds[0];
        pfd.events = POLLIN;
        poll(&pfd, 1, 100);
        if (pfd.revents & (POLLIN | POLLHUP)) {
            n = read(fds[0], tmp, sizeof tmp);
            if (n > 0 && used + 1 < sizeof acc) {
                size_t take = (size_t)n;
                if (take > sizeof acc - 1 - used) {
                    take = sizeof acc - 1 - used;
                }
                memcpy(acc + used, tmp, take);
                used += take;
                acc[used] = 0;
            }
        }
        got = waitpid(pid, &st, WNOHANG);
        if (got == pid || (got < 0 && errno != EINTR)) {
            break;
        }
    }
    for (;;) {
        char tmp[512];
        ssize_t n = read(fds[0], tmp, sizeof tmp);
        if (n <= 0) {
            break;
        }
        if (used + 1 < sizeof acc) {
            size_t take = (size_t)n;
            if (take > sizeof acc - 1 - used) {
                take = sizeof acc - 1 - used;
            }
            memcpy(acc + used, tmp, take);
            used += take;
            acc[used] = 0;
        }
    }
    close(fds[0]);
    g_exit = (st >= 0 && WIFEXITED(st)) ? WEXITSTATUS(st) : -1;
    store_capture(acc);
    return 0;
}

static void draw_cells(int rows, int cols)
{
    int gh;
    int gw;
    int i;
    WINDOW *wins[GRID];

    gh = (rows - 1) / 3;
    gw = cols / 3;
    for (i = 0; i < GRID; i++) {
        wins[i] = NULL;
    }
    for (i = 0; i < GRID; i++) {
        int r = i / 3;
        int c = i % 3;
        int y = r * gh;
        int x = c * gw;
        int h = (r == 2) ? (rows - 1 - 2 * gh) : gh;
        int w = (c == 2) ? (cols - 2 * gw) : gw;
        WINDOW *win;
        int line;
        int focus = (i == g_focus);

        if (h < 3 || w < 8) {
            continue;
        }
        win = newwin(h, w, y, x);
        if (!win) {
            continue;
        }
        wins[i] = win;
        if (focus && has_colors()) {
            wattron(win, COLOR_PAIR(1) | A_BOLD);
        } else if (focus) {
            wattron(win, A_BOLD);
        }
        box(win, 0, 0);
        mvwprintw(win, 0, 2, " %s ", k_title[i]);
        if (focus && has_colors()) {
            wattroff(win, COLOR_PAIR(1) | A_BOLD);
        } else if (focus) {
            wattroff(win, A_BOLD);
        }
        for (line = 0; line < g_ncell[i] && line + 2 < h; line++) {
            mvwprintw(win, line + 1, 1, "%.*s", w - 2, g_cell[i][line]);
        }
    }
    attron(A_REVERSE);
    mvhline(rows - 1, 0, ' ', cols > 0 ? cols - 1 : 0);
    mvaddnstr(rows - 1, 0, " pico2 | tab | j/k info volume | enter q",
              cols > 0 ? cols - 1 : 0);
    attroff(A_REVERSE);
    wnoutrefresh(stdscr);
    for (i = 0; i < GRID; i++) {
        if (wins[i]) {
            wnoutrefresh(wins[i]);
        }
    }
    doupdate();
    for (i = 0; i < GRID; i++) {
        if (wins[i]) {
            delwin(wins[i]);
        }
    }
}

static void draw(void)
{
    int rows;
    int cols;

    getmaxyx(stdscr, rows, cols);
    if (rows < 16 || cols < 40) {
        mvprintw(0, 0, "sln-pico needs at least 40x16");
        refresh();
        return;
    }
    draw_cells(rows, cols);
}

static int run_sample(void)
{
    int ok_board = 0;
    int ok_uf2 = 0;
    int ok_file = 0;
    int i;

    sample();
    for (i = 0; i < GRID; i++) {
        int line;
        printf("== %s\n", k_title[i]);
        for (line = 0; line < g_ncell[i]; line++) {
            printf("%s\n", g_cell[i][line]);
            if (i == 0 && strstr(g_cell[i][line], "2e8a:000f")) {
                ok_board = 1;
            }
            if (i == 4 && strstr(g_cell[i][line], "Board-ID: RP2350")) {
                ok_uf2 = 1;
            }
            if (i == 5 && strstr(g_cell[i][line], "INFO_UF2.TXT")) {
                ok_file = 1;
            }
        }
    }
    if (!ok_board || !ok_uf2 || !ok_file) {
        fprintf(stderr, "pico-sample: fail\n");
        return 1;
    }
    printf("pico-sample: pass\n");
    return 0;
}

int main(int argc, char **argv)
{
    struct sigaction sa;

    if (argc == 2 && strcmp(argv[1], "--sample") == 0) {
        return run_sample();
    }
    if (argc != 1) {
        fprintf(stderr, "usage: sln-pico [--sample]\n");
        return 2;
    }
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    printf("\033]0;pico2\007");
    fflush(stdout);
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(1000);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
    }
    while (!g_stop) {
        int ch;

        sample();
        draw();
        ch = getch();
        if (ch == ERR || ch == KEY_RESIZE) {
            continue;
        }
        if (ch == 'q') {
            break;
        }
        if (ch == '\t') {
            g_focus = (g_focus + 1) % GRID;
        } else if (ch == KEY_BTAB) {
            g_focus = (g_focus + GRID - 1) % GRID;
        } else if (ch == 'j' || ch == KEY_DOWN) {
            g_which = (g_which + 1) % ACTS;
        } else if (ch == 'k' || ch == KEY_UP) {
            g_which = (g_which + ACTS - 1) % ACTS;
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            run_act();
        }
    }
    endwin();
    return 0;
}
