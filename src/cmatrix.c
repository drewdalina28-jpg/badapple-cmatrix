/*
    cmatrix.c

    Copyright (C) 1999-2017 Chris Allegretta
    Copyright (C) 2017-Present Abishek V Ashok

    This file is part of cmatrix.

    cmatrix is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cmatrix is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cmatrix. If not, see <http://www.gnu.org/licenses/>.

*/

#define NCURSES_WIDECHAR 1

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <locale.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifndef EXCLUDE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#ifdef _WIN32
#include <ncurses/curses.h>
#else
#include <curses.h>
#endif
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#elif defined(HAVE_TERMIO_H)
#include <termio.h>
#endif

#ifdef __CYGWIN__
#define TIOCSTI 0x5412
#endif

/* Matrix typedef */
typedef struct cmatrix {
    int val;
    bool is_head;
} cmatrix;

/* Global variables */
int console = 0;
int xwindow = 0;
int lock = 0;
cmatrix **matrix = (cmatrix **) NULL;
int *length = NULL;  /* Length of cols in each line */
int *spaces = NULL;  /* Spaces left to fill */
int *updates = NULL; /* What does this do again? */
#ifndef _WIN32
volatile sig_atomic_t signal_status = 0; /* Indicates a caught signal */
#endif

int va_system(char *str, ...) {

    va_list ap;
    char buf[133];

    va_start(ap, str);
    vsnprintf(buf, sizeof(buf), str, ap);
    va_end(ap);
    return system(buf);
}

static void ba_audio_stop(void);

/* What we do when we're all set to exit */
void finish(void) {
    ba_audio_stop();
    curs_set(1);
    clear();
    refresh();
    resetty();
    endwin();
    if (console) {
#ifdef HAVE_CONSOLECHARS
        va_system("consolechars -d");
#elif defined(HAVE_SETFONT)
        va_system("setfont");
#endif
    }
    exit(0);
}

/* What we do when we're all set to exit */
void c_die(char *msg, ...) {

    va_list ap;

    curs_set(1);
    clear();
    refresh();
    resetty();
    endwin();

    if (console) {
#ifdef HAVE_CONSOLECHARS
        va_system("consolechars -d");
#elif defined(HAVE_SETFONT)
        va_system("setfont");
#endif
    }

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    exit(0);
}

void usage(void) {
    printf(" Usage: cmatrix -[abBcfhlsmVxk] [-u delay] [-C color] [-t tty] [-M message]\n");
    printf(" -a: Asynchronous scroll\n");
    printf(" -b: Bold characters on\n");
    printf(" -B: All bold characters (overrides -b)\n");
    printf(" -c: Use Japanese characters as seen in the original matrix. Requires appropriate fonts\n");
    printf(" -f: Force the linux $TERM type to be on\n");
    printf(" -l: Linux mode (uses matrix console font)\n");
    printf(" -L: Lock mode (can be closed from another terminal)\n");
    printf(" -o: Use old-style scrolling\n");
    printf(" -h: Print usage and exit\n");
    printf(" -n: No bold characters (overrides -b and -B, default)\n");
    printf(" -s: \"Screensaver\" mode, exits on first keystroke\n");
    printf(" -x: X window mode, use if your xterm is using mtx.pcf\n");
    printf(" -V: Print version information and exit\n");
    printf(" -M [message]: Prints your message in the center of the screen. Overrides -L's default message.\n");
    printf(" -u delay (0 - 10, default 4): Screen update delay\n");
    printf(" -C [color]: Use this color for matrix (default green)\n");
    printf(" -r: rainbow mode\n");
    printf(" -m: lambda mode\n");
    printf(" -k: Characters change while scrolling. (Works without -o opt.)\n");
    printf(" -t [tty]: Set tty to use\n");
    printf(" -P [movie]: Play a .bpam movie (e.g. Bad Apple!!) over the matrix\n");
    printf(" -N: Movie only, full screen, no matrix rain (use with -P)\n");
    printf(" -S [audio]: Audio track to play in sync with the movie (use with -P)\n");
}

void version(void) {
    printf(" CMatrix version %s (compiled %s, %s)\n",
        VERSION, __TIME__, __DATE__);
    printf("Email: abishekvashok@gmail.com\n");
    printf("Web: https://github.com/abishekvashok/cmatrix\n");
}

/* ---------------------------------------------------------------------------
 * Bad Apple movie playback.
 *
 * A movie file is a header followed by 4-bit grayscale frames (see render.py).
 * Frames are drawn on top of the matrix rain, so you get the classic Bad
 * Apple!! animation playing inside the Matrix.
 *
 * The animation is clocked from the audio when one is playing: mpv is asked
 * for its playback position over its IPC socket, which lets the picture lock
 * to the music exactly instead of merely running alongside it.
 * ------------------------------------------------------------------------- */

static unsigned char *ba_data = NULL;
static uint32_t ba_w = 0, ba_h = 0, ba_nframes = 0, ba_frame_us = 50000;
static uint32_t ba_frame = 0;
static int ba_mode = 0;            /* 0 = off, 1 = window over rain, 2 = fullscreen */
static long long ba_t0 = 0;        /* movie clock origin, microseconds */
static pid_t ba_audio_pid = -1;
static char ba_ipc_path[128] = { 0 };
static const char ba_ramp[] = ".:-=+*#%@";
#define BA_INK 96                  /* gray level where artwork starts */
#define BA_MARGIN_W 4              /* room left for rain in window mode */
#define BA_MARGIN_H 2
#define BA_HDR_SIZE 24

static long long ba_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/* Load a .bpam movie file into memory. Returns 0 on success. */
static int ba_load(const char *path) {
    FILE *f;
    unsigned char hdr[BA_HDR_SIZE];
    uint32_t version, w, h, fus, n;
    size_t fsz, total;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cmatrix: cannot open movie '%s': %s\n", path,
                strerror(errno));
        return -1;
    }
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        memcmp(hdr, "BPAM", 4) != 0) {
        fprintf(stderr, "cmatrix: '%s' is not a badapple movie file\n", path);
        fclose(f);
        return -1;
    }
    memcpy(&version, hdr + 4, 4);
    memcpy(&w, hdr + 8, 4);
    memcpy(&h, hdr + 12, 4);
    memcpy(&fus, hdr + 16, 4);
    memcpy(&n, hdr + 20, 4);
    if (version != 2) {
        fprintf(stderr, "cmatrix: movie '%s' has unsupported version %u\n", path,
                version);
        fclose(f);
        return -1;
    }
    if (w == 0 || h == 0 || fus == 0 || n == 0) {
        fprintf(stderr, "cmatrix: malformed movie header in '%s'\n", path);
        fclose(f);
        return -1;
    }
    fsz = ((size_t)w * h + 1) / 2;
    total = fsz * n;
    ba_data = malloc(total);
    if (!ba_data) {
        fprintf(stderr, "cmatrix: out of memory loading movie\n");
        fclose(f);
        return -1;
    }
    if (fread(ba_data, 1, total, f) != total) {
        fprintf(stderr, "cmatrix: truncated movie file '%s'\n", path);
        free(ba_data);
        ba_data = NULL;
        fclose(f);
        return -1;
    }
    fclose(f);
    ba_w = w;
    ba_h = h;
    ba_frame_us = fus;
    ba_nframes = n;
    return 0;
}

/* Draw the current frame, fitted to the terminal, keeping the aspect ratio. */
static void ba_draw(int bold, int mcolor) {
    int tw = (int)ba_w, th = (int)ba_h;
    int avail_w, avail_h, ox, oy, tx, ty;
    size_t fsz;
    const unsigned char *frame;

    if (!ba_data)
        return;

    if (ba_mode == 2) {
        avail_w = COLS;
        avail_h = LINES;
    } else {
        avail_w = COLS - BA_MARGIN_W;
        avail_h = LINES - BA_MARGIN_H;
        if (avail_w < 8)
            avail_w = COLS;
        if (avail_h < 8)
            avail_h = LINES;
    }
    if (tw > avail_w) {
        th = (int)((long)th * avail_w / tw);
        tw = avail_w;
    }
    if (th > avail_h) {
        tw = (int)((long)tw * avail_h / th);
        th = avail_h;
    }
    if (tw < 1)
        tw = 1;
    if (th < 1)
        th = 1;

    ox = (COLS - tw) / 2;
    oy = (LINES - th) / 2;

    if (ba_mode == 2)
        werase(stdscr);

    fsz = ((size_t)ba_w * ba_h + 1) / 2;
    frame = ba_data + (size_t)ba_frame * fsz;

    for (ty = 0; ty < th; ty++) {
        int sy0 = (int)((long)ty * ba_h / th);
        int sy1 = (int)((long)(ty + 1) * ba_h / th);
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        for (tx = 0; tx < tw; tx++) {
            int sx0 = (int)((long)tx * ba_w / tw);
            int sx1 = (int)((long)(tx + 1) * ba_w / tw);
            int sum = 0, cnt = 0, gray, idx, level;
            int sy, sx;

            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            for (sy = sy0; sy < sy1; sy++) {
                size_t rowbase = (size_t)sy * ba_w;
                for (sx = sx0; sx < sx1; sx++) {
                    size_t p = rowbase + sx;
                    unsigned char b = frame[p >> 1];
                    sum += (p & 1) ? (b & 0x0F) : (b >> 4);
                    cnt++;
                }
            }
            gray = (sum * 17) / cnt;     /* 0 .. 255 */

            if (gray < BA_INK) {
                /* background: blank it out */
                attrset(A_NORMAL);
                move(oy + ty, ox + tx);
                addch(' ');
                continue;
            }

            idx = (gray - BA_INK) * 9 / (256 - BA_INK);
            if (idx > 8)
                idx = 8;
            level = (idx >= 7) ? 3 : (idx >= 3 ? 2 : 1);

            switch (level) {
                case 3:
                    attron(COLOR_PAIR(COLOR_WHITE));
                    if (bold)
                        attron(A_BOLD);
                    break;
                case 2:
                    attron(COLOR_PAIR(mcolor));
                    if (bold)
                        attron(A_BOLD);
                    break;
                default:
                    attron(COLOR_PAIR(mcolor));
                    attron(A_DIM);
                    break;
            }
            move(oy + ty, ox + tx);
            addch(ba_ramp[idx]);
            attrset(A_NORMAL);
        }
    }
}

/* Advance the movie clock and pick the frame for the current moment. */
static void ba_tick(void) {
    long long elapsed, index, next, now, sleep_us;

    if (!ba_data)
        return;
    now = ba_now_us();
    elapsed = now - ba_t0;
    if (elapsed < 0)
        elapsed = 0;
    index = elapsed / ba_frame_us;
    ba_frame = (uint32_t)(index % (long long)ba_nframes);

    /* sleep until the next frame is due */
    next = ba_t0 + (index + 1) * ba_frame_us;
    sleep_us = next - ba_now_us();
    if (sleep_us > 0)
        usleep((useconds_t)sleep_us);
}

/* --------------------------------------------------------------------------
 * Audio. The picture is clocked from the sound, so mpv is asked for its
 * playback position over its IPC socket. Whatever mpv reports as "position 0"
 * becomes the origin of the movie clock, which keeps the two locked together
 * (and looping in step) for as long as it runs.
 * ------------------------------------------------------------------------ */

static void ba_audio_stop(void) {
    if (ba_audio_pid > 0) {
        kill(ba_audio_pid, SIGTERM);
        waitpid(ba_audio_pid, NULL, WNOHANG);
        ba_audio_pid = -1;
    }
    if (ba_ipc_path[0]) {
        unlink(ba_ipc_path);
        ba_ipc_path[0] = '\0';
    }
}

/* Start looping the audio in the background. Returns the child pid or -1. */
static pid_t ba_audio_spawn(const char *path) {
    pid_t pid = fork();

    if (pid < 0)
        return -1;
    if (pid == 0) {
        int fd = open("/dev/null", O_RDWR);
        char ipcarg[160];
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2)
                close(fd);
        }
        if (ba_ipc_path[0]) {
            snprintf(ipcarg, sizeof(ipcarg), "--input-ipc-server=%s",
                     ba_ipc_path);
            execlp("mpv", "mpv", "--no-video", "--no-terminal", "--really-quiet",
                   "--loop=inf", ipcarg, path, (char *) NULL);
        } else {
            execlp("mpv", "mpv", "--no-video", "--no-terminal", "--really-quiet",
                   "--loop=inf", path, (char *) NULL);
        }
        /* fall back to vlc */
        execlp("vlc", "vlc", "--intf", "dummy", "--no-video", "--loop", path,
               (char *) NULL);
        _exit(127);
    }
    return pid;
}

/* Ask mpv for its playback position over the IPC socket. Returns 0 on success. */
static int ba_ipc_timepos(double *out) {
    struct sockaddr_un addr;
    struct timeval tv;
    char req[] = "{\"command\":[\"get_property\",\"time-pos\"]}\n";
    char resp[512];
    const char *p;
    ssize_t n;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ba_ipc_path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    tv.tv_sec = 0;
    tv.tv_usec = 300000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (write(fd, req, strlen(req)) < 0) {
        close(fd);
        return -1;
    }
    n = read(fd, resp, sizeof(resp) - 1);
    close(fd);
    if (n <= 0)
        return -1;
    resp[n] = '\0';
    p = strstr(resp, "\"data\":");
    if (!p)
        return -1;
    p += 7;
    *out = strtod(p, NULL);
    return 0;
}

/* Wait for mpv to report a position, then align the movie clock to it. */
static void ba_audio_sync(void) {
    double pos = 0.0;
    int tries;

    for (tries = 0; tries < 40; tries++) {
        if (ba_ipc_timepos(&pos) == 0 && pos > 0.02)
            break;
        usleep(100000);
    }
    if (pos > 0.02) {
        /* the media was at position 0 this long ago */
        ba_t0 = ba_now_us() - (long long)(pos * 1000000.0);
    } else {
        /* no IPC: fall back to a fixed guess at the startup delay */
        const char *d = getenv("BADAPPLE_AUDIO_DELAY");
        long delay_ms = d ? atol(d) : 350;
        usleep((useconds_t)delay_ms * 1000);
        ba_t0 = ba_now_us();
    }
}


/* nmalloc from nano by Big Gaute */
void *nmalloc(size_t howmuch) {
    void *r;

    if (!(r = malloc(howmuch))) {
        c_die("CMatrix: malloc: out of memory!");
    }

    return r;
}

/* Initialize the global variables */
void var_init() {
    int i, j;

    if (matrix != NULL) {
        free(matrix[0]);
        free(matrix);
    }

    matrix = nmalloc(sizeof(cmatrix *) * (LINES + 1));
    matrix[0] = nmalloc(sizeof(cmatrix) * (LINES + 1) * COLS);
    for (i = 1; i <= LINES; i++) {
        matrix[i] = matrix[i - 1] + COLS;
    }

    if (length != NULL) {
        free(length);
    }
    length = nmalloc(COLS * sizeof(int));

    if (spaces != NULL) {
        free(spaces);
    }
    spaces = nmalloc(COLS* sizeof(int));

    if (updates != NULL) {
        free(updates);
    }
    updates = nmalloc(COLS * sizeof(int));

    /* Make the matrix */
    for (i = 0; i <= LINES; i++) {
        for (j = 0; j <= COLS - 1; j += 2) {
            matrix[i][j].val = -1;
        }
    }

    for (j = 0; j <= COLS - 1; j += 2) {
        /* Set up spaces[] array of how many spaces to skip */
        spaces[j] = (int) rand() % LINES + 1;

        /* And length of the stream */
        length[j] = (int) rand() % (LINES - 3) + 3;

        /* Sentinel value for creation of new objects */
        matrix[1][j].val = ' ';

        /* And set updates[] array for update speed. */
        updates[j] = (int) rand() % 3 + 1;
    }

}

#ifndef _WIN32
void sighandler(int s) {
    signal_status = s;
}
#endif

void resize_screen(void) {
#ifdef _WIN32
    BOOL result;
    HANDLE hStdHandle;
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;

    hStdHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdHandle == INVALID_HANDLE_VALUE)
        return;
#else
    char *tty;
    int fd = 0;
    int result = 0;
    struct winsize win;

    tty = ttyname(0);
    if (!tty) {
        return;
#endif
#ifdef _WIN32
    result = GetConsoleScreenBufferInfo(hStdHandle, &csbiInfo);
    if (!result)
        return;
    LINES = csbiInfo.dwSize.Y;
    COLS = csbiInfo.dwSize.X;
#else
    }
    fd = open(tty, O_RDWR);
    if (fd == -1) {
        return;
    }
    result = ioctl(fd, TIOCGWINSZ, &win);
    if (result == -1) {
        return;
    }

    COLS = win.ws_col;
    LINES = win.ws_row;
#endif

    if (LINES < 10) {
        LINES = 10;
    }
    if (COLS < 10) {
        COLS = 10;
    }

#ifdef HAVE_RESIZETERM
    resizeterm(LINES, COLS);
#ifdef HAVE_WRESIZE
    if (wresize(stdscr, LINES, COLS) == ERR) {
        c_die("Cannot resize window!");
    }
#endif /* HAVE_WRESIZE */
#endif /* HAVE_RESIZETERM */

    var_init();
    /* Do these because width may have changed... */
    clear();
    refresh();
}

int main(int argc, char *argv[]) {
    int i, y, z, optchr, keypress;
    int j = 0;
    int count = 0;
    int screensaver = 0;
    int asynch = 0;
    int bold = 0;
    int force = 0;
    int firstcoldone = 0;
    int oldstyle = 0;
    int random = 0;
    int update = 4;
    int highnum = 0;
    int mcolor = COLOR_GREEN;
    int rainbow = 0;
    int lambda = 0;
    int randnum = 0;
    int randmin = 0;
    int pause = 0;
    int classic = 0;
    int changes = 0;
    char *msg = "";
    char *tty = NULL;
    char *movie = NULL;
    char *audio = NULL;

    srand((unsigned) time(NULL));
    setlocale(LC_ALL, "");

    /* Many thanks to morph- (morph@jmss.com) for this getopt patch */
    opterr = 0;
    while ((optchr = getopt(argc, argv, "abBcfhlLnrosmxkVM:u:C:t:P:N:S:")) != EOF) {
        switch (optchr) {
        case 's':
            screensaver = 1;
            break;
        case 'a':
            asynch = 1;
            break;
        case 'b':
            if (bold != 2) {
                bold = 1;
            }
            break;
        case 'B':
            bold = 2;
            break;
        case 'C':
            if (!strcasecmp(optarg, "green")) {
                mcolor = COLOR_GREEN;
            } else if (!strcasecmp(optarg, "red")) {
                mcolor = COLOR_RED;
            } else if (!strcasecmp(optarg, "blue")) {
                mcolor = COLOR_BLUE;
            } else if (!strcasecmp(optarg, "white")) {
                mcolor = COLOR_WHITE;
            } else if (!strcasecmp(optarg, "yellow")) {
                mcolor = COLOR_YELLOW;
            } else if (!strcasecmp(optarg, "cyan")) {
                mcolor = COLOR_CYAN;
            } else if (!strcasecmp(optarg, "magenta")) {
                mcolor = COLOR_MAGENTA;
            } else if (!strcasecmp(optarg, "black")) {
                mcolor = COLOR_BLACK;
            } else {
                c_die(" Invalid color selection\n Valid "
                       "colors are green, red, blue, "
                       "white, yellow, cyan, magenta " "and black.\n");
            }
            break;
        case 'c':
            classic = 1;
            break;
        case 'f':
            force = 1;
            break;
        case 'l':
            console = 1;
            break;
        case 'L':
            lock = 1;
            //if -M was used earlier, don't override it
            if (0 == strncmp(msg, "", 1)) {
                msg = "Computer locked.";
            }
            break;
        case 'M':
            msg = strdup(optarg);
            break;
        case 'n':
            bold = -1;
            break;
        case 'h':
        case '?':
            usage();
            exit(0);
        case 'o':
            oldstyle = 1;
            break;
        case 'u':
            update = atoi(optarg);
            break;
        case 'x':
            xwindow = 1;
            break;
        case 'V':
            version();
            exit(0);
        case 'r':
            rainbow = 1;
            break;
        case 'm':
            lambda = 1;
            break;
        case 'k':
            changes = 1;
            break;
        case 't':
            tty = optarg;
            break;
        case 'P':
            movie = optarg;
            ba_mode = 1;
            break;
        case 'N':
            ba_mode = 2;
            break;
        case 'S':
            audio = optarg;
            break;
        }
    }

    /* Clear TERM variable on Windows */
#ifdef _WIN32
    _putenv_s("TERM", "");
#endif

    if (force && strcmp("linux", getenv("TERM"))) {
#ifdef _WIN32
        SetEnvironmentVariableW(L"TERM", L"linux");
#else
        /* setenv is much more safe to use than putenv */
        setenv("TERM", "linux", 1);
#endif
    }
    if (tty) {
        FILE *ftty = fopen(tty, "r+");
        if (!ftty) {
            fprintf(stderr, "cmatrix: error: '%s' couldn't be opened: %s.\n",
                    tty, strerror(errno));
            exit(EXIT_FAILURE);
        }
        SCREEN *ttyscr;
        ttyscr = newterm(NULL, ftty, ftty);
        if (ttyscr == NULL)
            exit(EXIT_FAILURE);
        set_term(ttyscr);
    } else
        initscr();
    savetty();
    nonl();
#ifdef _WIN32
    raw();
#else
    cbreak();
#endif
    noecho();
    timeout(0);
    leaveok(stdscr, TRUE);
    curs_set(0);
#ifndef _WIN32
    signal(SIGINT, sighandler);
    signal(SIGQUIT, sighandler);
    signal(SIGWINCH, sighandler);
    signal(SIGTSTP, sighandler);
    signal(SIGHUP, sighandler);
#endif

if (console) {
#ifdef HAVE_CONSOLECHARS
        if (va_system("consolechars -f matrix") != 0) {
            c_die
                (" There was an error running consolechars. Please make sure the\n"
                 " consolechars program is in your $PATH.  Try running \"consolechars -f matrix\" by hand.\n");
        }
#elif defined(HAVE_SETFONT)
        if (va_system("setfont matrix") != 0) {
            c_die
                (" There was an error running setfont. Please make sure the\n"
                 " setfont program is in your $PATH.  Try running \"setfont matrix\" by hand.\n");
        }
#else
        c_die(" Unable to use both \"setfont\" and \"consolechars\".\n");
#endif
}
    if (has_colors()) {
        start_color();
        /* Add in colors, if available */
#ifdef HAVE_USE_DEFAULT_COLORS
        if (use_default_colors() != ERR) {
            init_pair(COLOR_BLACK, -1, -1);
            init_pair(COLOR_GREEN, COLOR_GREEN, -1);
            init_pair(COLOR_WHITE, COLOR_WHITE, -1);
            init_pair(COLOR_RED, COLOR_RED, -1);
            init_pair(COLOR_CYAN, COLOR_CYAN, -1);
            init_pair(COLOR_MAGENTA, COLOR_MAGENTA, -1);
            init_pair(COLOR_BLUE, COLOR_BLUE, -1);
            init_pair(COLOR_YELLOW, COLOR_YELLOW, -1);
        } else {
#else
        { /* Hack to deal the after effects of else in HAVE_USE_DEFAULT_COLOURS */
#endif
            init_pair(COLOR_BLACK, COLOR_BLACK, COLOR_BLACK);
            init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
            init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
            init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
            init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
            init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
            init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
            init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
        }
    }

    /* Set up values for random number generation */
    if (classic) {
        /* Half-width kana characters. In the movie they are y-axis flipped, and
         * they appear alongside latin characters and numerals, but this is the
         * closest we can do with a standard unicode set and a single number
         * range */
        randmin = 0xff66;
        highnum = 0xff9d;
    } else if (console || xwindow) {
        randmin = 166;
        highnum = 217;
    } else {
        randmin = 33;
        highnum = 123;
    }
    randnum = highnum - randmin;

    var_init();

    if (ba_mode && movie) {
        if (ba_load(movie) != 0) {
            finish();
            exit(EXIT_FAILURE);
        }
        fprintf(stderr, "cmatrix: playing %s (%ux%u, %u frames)\n", movie,
                ba_w, ba_h, ba_nframes);
    } else {
        ba_mode = 0;
    }

    if (ba_mode && audio) {
        const char *tmp = getenv("TMPDIR");
        if (!tmp || !*tmp)
            tmp = "/tmp";
        snprintf(ba_ipc_path, sizeof(ba_ipc_path), "%s/badapple-mpv-%d.sock",
                 tmp, (int) getpid());
        unlink(ba_ipc_path);
        ba_audio_pid = ba_audio_spawn(audio);
        if (ba_audio_pid < 0) {
            fprintf(stderr, "cmatrix: could not start the audio player\n");
            ba_ipc_path[0] = '\0';
        } else {
            ba_audio_sync();
        }
    } else {
        ba_t0 = ba_now_us();
    }

    while (1) {
#ifndef _WIN32
        /* Check for signals */
        if (signal_status == SIGINT || signal_status == SIGQUIT ||
            signal_status == SIGHUP) {
            if (lock != 1)
                finish();
            /* exits */
        }
        if (signal_status == SIGWINCH) {
            resize_screen();
            signal_status = 0;
        }

        if (signal_status == SIGTSTP) {
            if (lock != 1)
                    finish();
        }
#endif

        count++;
        if (count > 4) {
            count = 1;
        }

        if ((keypress = wgetch(stdscr)) != ERR) {
            if (screensaver == 1) {
#ifdef USE_TIOCSTI
                char *str = malloc(0);
                size_t str_len = 0;
                do {
                    str = realloc(str, str_len + 1);
                    str[str_len++] = keypress;
                } while ((keypress = wgetch(stdscr)) != ERR);
                size_t i;
                for (i = 0; i < str_len; i++)
                    ioctl(STDIN_FILENO, TIOCSTI, (char*)(str + i));
                free(str);
#endif
                finish();
            } else {
                switch (keypress) {
#ifdef _WIN32
                case 3: /* Ctrl-C. Fall through */
#endif
                case 'q':
                    if (lock != 1)
                        finish();
                    break;
                case 'a':
                    asynch = 1 - asynch;
                    break;
                case 'b':
                    bold = 1;
                    break;
                case 'B':
                    bold = 2;
                    break;
                case 'L':
                    lock = 1;
                    break;
                case 'n':
                    bold = 0;
                    break;
                case '0': /* Fall through */
                case '1': /* Fall through */
                case '2': /* Fall through */
                case '3': /* Fall through */
                case '4': /* Fall through */
                case '5': /* Fall through */
                case '6': /* Fall through */
                case '7': /* Fall through */
                case '8': /* Fall through */
                case '9':
                    update = keypress - 48;
                    break;
                case '!':
                    mcolor = COLOR_RED;
                    rainbow = 0;
                    break;
                case '@':
                    mcolor = COLOR_GREEN;
                    rainbow = 0;
                    break;
                case '#':
                    mcolor = COLOR_YELLOW;
                    rainbow = 0;
                    break;
                case '$':
                    mcolor = COLOR_BLUE;
                    rainbow = 0;
                    break;
                case '%':
                    mcolor = COLOR_MAGENTA;
                    rainbow = 0;
                    break;
                case 'r':
                     rainbow = 1;
                     break;
                case 'm':
                     lambda = !lambda;
                     break;
                case '^':
                    mcolor = COLOR_CYAN;
                    rainbow = 0;
                    break;
                case '&':
                    mcolor = COLOR_WHITE;
                    rainbow = 0;
                    break;
                case 'p':
                case 'P':
                    pause = (pause == 0)?1:0;
                    break;

                }
            }
        }
        for (j = 0; j <= COLS - 1; j += 2) {
            if ((count > updates[j] || asynch == 0) && pause == 0) {

                /* I don't like old-style scrolling, yuck */
                if (oldstyle) {
                    for (i = LINES - 1; i >= 1; i--) {
                        matrix[i][j].val = matrix[i - 1][j].val;
                    }
                    random = (int) rand() % (randnum + 8) + randmin;

                    if (matrix[1][j].val == 0) {
                        matrix[0][j].val = 1;
                    } else if (matrix[1][j].val == ' '
                             || matrix[1][j].val == -1) {
                        if (spaces[j] > 0) {
                            matrix[0][j].val = ' ';
                            spaces[j]--;
                        } else {

                            /* Random number to determine whether head of next column
                               of chars has a white 'head' on it. */

                            if (((int) rand() % 3) == 1) {
                                matrix[0][j].val = 0;
                            } else {
                                matrix[0][j].val = (int) rand() % randnum + randmin;
                            }
                            spaces[j] = (int) rand() % LINES + 1;
                        }
                    } else if (random > highnum && matrix[1][j].val != 1) {
                        matrix[0][j].val = ' ';
                    } else {
                        matrix[0][j].val = (int) rand() % randnum + randmin;
                    }

                } else { /* New style scrolling (default) */
                    if (matrix[0][j].val == -1 && matrix[1][j].val == ' '
                        && spaces[j] > 0) {
                        spaces[j]--;
                    } else if (matrix[0][j].val == -1
                        && matrix[1][j].val == ' ') {
                        length[j] = (int) rand() % (LINES - 3) + 3;
                        matrix[0][j].val = (int) rand() % randnum + randmin;

                        spaces[j] = (int) rand() % LINES + 1;
                    }
                    i = 0;
                    y = 0;
                    firstcoldone = 0;
                    while (i <= LINES) {

                        /* Skip over spaces */
                        while (i <= LINES && (matrix[i][j].val == ' ' ||
                               matrix[i][j].val == -1)) {
                            i++;
                        }

                        if (i > LINES) {
                            break;
                        }

                        /* Go to the head of this column */
                        z = i;
                        y = 0;
                        while (i <= LINES && (matrix[i][j].val != ' ' &&
                               matrix[i][j].val != -1)) {
                            matrix[i][j].is_head = false;
                            if (changes) {
                                if (rand() % 8 == 0)
                                    matrix[i][j].val = (int) rand() % randnum + randmin;
                            }
                            i++;
                            y++;
                        }

                        if (i > LINES) {
                            matrix[z][j].val = ' ';
                            continue;
                        }

                        matrix[i][j].val = (int) rand() % randnum + randmin;
                        matrix[i][j].is_head = true;

                        /* If we're at the top of the column and it's reached its
                           full length (about to start moving down), we do this
                           to get it moving.  This is also how we keep segments not
                           already growing from growing accidentally =>
                         */
                        if (y > length[j] || firstcoldone) {
                            matrix[z][j].val = ' ';
                            matrix[0][j].val = -1;
                        }
                        firstcoldone = 1;
                        i++;
                    }
                }
            }
            /* A simple hack */
            if (!oldstyle) {
                y = 1;
                z = LINES;
            } else {
                y = 0;
                z = LINES - 1;
            }
            for (i = y; i <= z; i++) {
                move(i - y, j);

                if (matrix[i][j].val == 0 || (matrix[i][j].is_head && !rainbow)) {
                    if (console || xwindow) {
                        attron(A_ALTCHARSET);
                    }
                    attron(COLOR_PAIR(COLOR_WHITE));
                    if (bold) {
                        attron(A_BOLD);
                    }
                    if (matrix[i][j].val == 0) {
                        if (console || xwindow) {
                            addch(183);
                        } else {
                            addch('&');
                        }
                    } else if (matrix[i][j].val == -1) {
                        addch(' ');
                    } else {
                        addch(matrix[i][j].val);
                    }

                    attroff(COLOR_PAIR(COLOR_WHITE));
                    if (bold) {
                        attroff(A_BOLD);
                    }
                    if (console || xwindow) {
                        attroff(A_ALTCHARSET);
                    }
                } else {
                    if (rainbow) {
                        int randomColor = rand() % 6;

                        switch (randomColor) {
                            case 0:
                                mcolor = COLOR_GREEN;
                                break;
                            case 1:
                                mcolor = COLOR_BLUE;
                                break;
                            case 2:
                                mcolor = COLOR_BLACK;
                                break;
                            case 3:
                                mcolor = COLOR_YELLOW;
                                break;
                            case 4:
                                mcolor = COLOR_CYAN;
                                break;
                            case 5:
                                mcolor = COLOR_MAGENTA;
                                break;
                       }
                    }
                    attron(COLOR_PAIR(mcolor));
                    if (matrix[i][j].val == 1) {
                        if (bold) {
                            attron(A_BOLD);
                        }
                        addch('|');
                        if (bold) {
                            attroff(A_BOLD);
                        }
                    } else {
                        if (console || xwindow) {
                            attron(A_ALTCHARSET);
                        }
                        if (bold == 2 ||
                            (bold == 1 && matrix[i][j].val % 2 == 0)) {
                            attron(A_BOLD);
                        }
                        if (matrix[i][j].val == -1) {
                            addch(' ');
                        } else if (lambda && matrix[i][j].val != ' ') {
                            addstr("λ");
                        } else {
                            /* addch doesn't seem to work with unicode
                             * characters and there was no direct equivalent.
                             * So, construct a c-style string with the character
                             * and print that.
                             */
                            wchar_t char_array[2];
                            char_array[0] = matrix[i][j].val;
                            char_array[1] = 0;
                            addwstr(char_array);
                        }
                        if (bold == 2 ||
                            (bold == 1 && matrix[i][j].val % 2 == 0)) {
                            attroff(A_BOLD);
                        }
                        if (console || xwindow) {
                            attroff(A_ALTCHARSET);
                        }
                    }
                    attroff(COLOR_PAIR(mcolor));
                }
            }
        }

        /* Bad Apple: advance the movie clock and paint the frame over the rain */
        if (ba_mode) {
            ba_tick();
            ba_draw(bold, mcolor);
        }

        //check if -M and/or -L was used
        if (msg[0] != '\0') {            //Add our message to the screen
            int msg_x = LINES/2;
            int msg_y = COLS/2 - strlen(msg)/2;
            int i = 0;

            //Add space before message
            move(msg_x-1, msg_y-2);
            for (i = 0; i < strlen(msg)+4; i++)
                addch(' ');

            //Write message
            move(msg_x, msg_y-2);
            addch(' ');
            addch(' ');
            addstr(msg);
            addch(' ');
            addch(' ');

            //Add space after message
            move(msg_x+1, msg_y-2);
            for (i = 0; i < strlen(msg)+4; i++)
                addch(' ');
        }

        if (!ba_mode) {
            napms(update * 10);
        }
    }
    finish();
}
