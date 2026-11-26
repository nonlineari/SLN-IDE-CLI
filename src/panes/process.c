#include "inject.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { SLN_PROC_CAP = 512, SLN_TOP_N = 8 };

typedef struct {
    int pid;
    unsigned long ticks;
} tick_rec;

typedef struct {
    int pid;
    unsigned long delta;
    char comm[64];
} top_rec;

static tick_rec old_tab[SLN_PROC_CAP];
static int nold;
static unsigned long long prev_total;
static unsigned long long prev_idle;
static int have_cpu;

static int all_digits(const char *s)
{
    if (!s || !s[0]) {
        return 0;
    }
    while (*s) {
        if (!isdigit((unsigned char)*s)) {
            return 0;
        }
        s++;
    }
    return 1;
}

static int find_old(int pid, unsigned long *ticks)
{
    int i;

    for (i = 0; i < nold; i++) {
        if (old_tab[i].pid == pid) {
            *ticks = old_tab[i].ticks;
            return 1;
        }
    }
    return 0;
}

static int cmp_top(const void *a, const void *b)
{
    const top_rec *pa = a;
    const top_rec *pb = b;

    if (pa->delta < pb->delta) {
        return 1;
    }
    if (pa->delta > pb->delta) {
        return -1;
    }
    return 0;
}

static int read_cpu(unsigned long long *total, unsigned long long *idle_all)
{
    FILE *f;
    char line[256];
    unsigned long long fild[8];
    int i;
    int got;

    f = fopen("/proc/stat", "r");
    if (!f) {
        return -1;
    }
    if (!fgets(line, (int)sizeof line, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    if (strncmp(line, "cpu ", 4) != 0) {
        return -1;
    }
    for (i = 0; i < 8; i++) {
        fild[i] = 0;
    }
    got = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                 &fild[0], &fild[1], &fild[2], &fild[3],
                 &fild[4], &fild[5], &fild[6], &fild[7]);
    if (got < 4) {
        return -1;
    }
    *idle_all = fild[3] + fild[4];
    *total = 0;
    for (i = 0; i < 8; i++) {
        *total += fild[i];
    }
    return 0;
}

static int read_proc(int pid, unsigned long *ticks, char *comm, size_t ncomm)
{
    char path[64];
    char buf[1024];
    char *rpar;
    unsigned long utime = 0;
    unsigned long stime = 0;
    FILE *f;

    snprintf(path, sizeof path, "/proc/%d/comm", pid);
    if (sln_read_str(path, comm, ncomm) != 0) {
        comm[0] = 0;
    }
    snprintf(path, sizeof path, "/proc/%d/stat", pid);
    f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    if (!fgets(buf, (int)sizeof buf, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    rpar = strrchr(buf, ')');
    if (!rpar) {
        return -1;
    }
    /* state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime */
    {
        const char *p = rpar + 1;
        int field;
        for (field = 0; field < 11; field++) {
            while (*p == ' ') {
                p++;
            }
            if (*p == 0) {
                return -1;
            }
            while (*p && *p != ' ') {
                p++;
            }
        }
        while (*p == ' ') {
            p++;
        }
        if (sscanf(p, "%lu %lu", &utime, &stime) != 2) {
            return -1;
        }
    }
    *ticks = utime + stime;
    return 0;
}

static void sample(sln_ctx *ctx)
{
    unsigned long long total = 0;
    unsigned long long idle_all = 0;
    unsigned long long total_d = 0;
    long ncpu;
    char load[128];
    char a[32];
    char b[32];
    char c[32];
    FILE *mem;
    char line[256];
    long mem_total = 0;
    long mem_avail = 0;
    DIR *dir;
    struct dirent *de;
    tick_rec now_tab[SLN_PROC_CAP];
    top_rec top[SLN_PROC_CAP];
    int nnow = 0;
    int ntop = 0;
    int scanned = 0;
    int i;
    int capped = 0;

    ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) {
        ncpu = 1;
    }
    if (read_cpu(&total, &idle_all) == 0 && have_cpu && total >= prev_total) {
        total_d = total - prev_total;
        if (idle_all >= prev_idle && total_d > 0) {
            unsigned long long busy = total_d - (idle_all - prev_idle);
            unsigned pct = (unsigned)((busy * 100ull) / total_d);
            sln_add(ctx, "cpu %u%%   %ld cores", pct, ncpu);
        } else {
            sln_add(ctx, "cpu sampling   %ld cores", ncpu);
        }
    } else {
        sln_add(ctx, "cpu sampling   %ld cores", ncpu);
    }
    if (read_cpu(&total, &idle_all) == 0) {
        prev_total = total;
        prev_idle = idle_all;
        have_cpu = 1;
    }

    a[0] = b[0] = c[0] = 0;
    if (sln_read_str("/proc/loadavg", load, sizeof load) == 0 &&
        sscanf(load, "%31s %31s %31s", a, b, c) == 3) {
        sln_add(ctx, "load %s %s %s", a, b, c);
    } else {
        sln_add(ctx, "load unavailable");
    }

    mem = fopen("/proc/meminfo", "r");
    if (mem) {
        while (fgets(line, (int)sizeof line, mem)) {
            if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1) {
                continue;
            }
            if (sscanf(line, "MemAvailable: %ld kB", &mem_avail) == 1) {
                break;
            }
        }
        fclose(mem);
    }
    if (mem_total > 0) {
        long used = mem_total - mem_avail;
        if (used < 0) {
            used = 0;
        }
        sln_add(ctx, "mem %ld / %ld MiB", used / 1024, mem_total / 1024);
    }

    dir = opendir("/proc");
    if (!dir) {
        sln_add_str(ctx, "proc unreadable");
        return;
    }
    while ((de = readdir(dir)) != NULL) {
        int pid;
        unsigned long ticks = 0;
        unsigned long old_ticks = 0;
        char comm[64];
        int known;

        if (!all_digits(de->d_name)) {
            continue;
        }
        if (scanned >= SLN_PROC_CAP) {
            capped = 1;
            break;
        }
        scanned++;
        pid = atoi(de->d_name);
        if (pid <= 0) {
            continue;
        }
        comm[0] = 0;
        if (read_proc(pid, &ticks, comm, sizeof comm) != 0) {
            continue;
        }
        if (nnow < SLN_PROC_CAP) {
            now_tab[nnow].pid = pid;
            now_tab[nnow].ticks = ticks;
            nnow++;
        }
        known = find_old(pid, &old_ticks);
        if (known && ticks >= old_ticks && ntop < SLN_PROC_CAP) {
            size_t k = 0;
            top[ntop].pid = pid;
            top[ntop].delta = ticks - old_ticks;
            while (comm[k] && k + 1 < sizeof top[ntop].comm) {
                top[ntop].comm[k] = comm[k];
                k++;
            }
            top[ntop].comm[k] = 0;
            ntop++;
        }
    }
    closedir(dir);
    nold = nnow;
    if (nnow > 0) {
        memcpy(old_tab, now_tab, (size_t)nnow * sizeof now_tab[0]);
    }
    qsort(top, (size_t)ntop, sizeof top[0], cmp_top);
    if (capped) {
        sln_add(ctx, "scan capped at %d", SLN_PROC_CAP);
    }
    if (!have_cpu || total_d == 0) {
        sln_add_str(ctx, "processes sampling");
        return;
    }
    for (i = 0; i < ntop && i < SLN_TOP_N; i++) {
        unsigned pct = 0;
        if (top[i].delta == 0) {
            break;
        }
        pct = (unsigned)(((unsigned long long)top[i].delta * 100ull * (unsigned long long)ncpu) / total_d);
        sln_add(ctx, "%3u%%  %d  %s", pct, top[i].pid, top[i].comm);
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

const sln_pane sln_pane_process = {
    "process",
    "cpu memory tasks",
    sample,
    draw,
    key
};
