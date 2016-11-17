/*
Copyright (c) 1998-2015, Enno Rehling <enno@eressea.de>
Katja Zedel <katze@felidae.kn-bremen.de
Christian Schlittchen <corwin@amber.kn-bremen.de>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
**/

#include <platform.h>
#include <util/log.h>

#include <kernel/config.h>
#include <kernel/version.h>
#include <kernel/save.h>
#include <util/filereader.h>
#include <util/language.h>
#include "eressea.h"
#include "battle.h"
#ifdef USE_CURSES
#include "gmtool.h"
#endif

#include "bindings.h"
#include "races/races.h"
#include "spells.h"

#include <lua.h>
#include <assert.h>
#include <locale.h>
#include <wctype.h>
#include <iniparser.h>

static const char *logfile = "eressea.log";
static const char *luafile = 0;
static const char *inifile = "eressea.ini";
static int memdebug = 0;
static int verbosity = 1;

static void load_inifile(dictionary * d)
{
    const char *reportdir = reportpath();
    const char *datadir = datapath();
    const char *basedir = basepath();
    const char *str;

    assert(d);

    str = iniparser_getstring(d, "eressea:base", basedir);
    if (str != basedir) {
        set_basepath(str);
    }
    str = iniparser_getstring(d, "eressea:report", reportdir);
    if (str != reportdir) {
        set_reportpath(str);
    }
    str = iniparser_getstring(d, "eressea:data", datadir);
    if (str != datadir) {
        set_datapath(str);
    }

    lomem = iniparser_getint(d, "eressea:lomem", lomem) ? 1 : 0;

    str = iniparser_getstring(d, "eressea:encoding", NULL);
    if (str && (_strcmpl(str, "utf8") == 0 || _strcmpl(str, "utf-8") == 0)) {
        enc_gamedata = ENCODING_UTF8;
    }

    verbosity = iniparser_getint(d, "eressea:verbose", 2);
    battledebug = iniparser_getint(d, "eressea:debug", battledebug) ? 1 : 0;

    str = iniparser_getstring(d, "eressea:locales", "de,en");
    make_locales(str);

    if (global.inifile) iniparser_freedict(global.inifile);
    global.inifile = d;
}

static void parse_config(const char *filename)
{
    dictionary *d = iniparser_load(filename);
    if (d) {
        load_inifile(d);
        log_debug("reading from configuration file %s\n", filename);

        memdebug = iniparser_getint(d, "eressea:memcheck", memdebug);
#ifdef USE_CURSES
        /* only one value in the [editor] section */
        force_color = iniparser_getint(d, "editor:color", force_color);
        gm_codepage = iniparser_getint(d, "editor:codepage", gm_codepage);
#endif
    }
}

static int usage(const char *prog, const char *arg)
{
    if (arg) {
        fprintf(stderr, "unknown argument: %s\n\n", arg);
    }
    fprintf(stderr, "Usage: %s [options]\n"
        "-t <turn>        : read this datafile, not the most current one\n"
        "-f <script.lua>  : execute a lua script\n"
        "-q               : be quite (same as -v 0)\n"
        "-v <level>       : verbosity level\n"
        "-C               : run in interactive mode\n"
        "--color          : force curses to use colors even when not detected\n", prog);
    return -1;
}

static int get_arg(int argc, char **argv, size_t len, int index, const char **result, const char *def) {
    if (argv[index][len]) {
        *result = argv[index] + len;
        return index;
    }
    if (index + 1 < argc) {
        *result = argv[index + 1];
        return index + 1;
    }
    *result = def;
    return index;
}

static int verbosity_to_flags(int verbosity) {
    int flags = 0;
    switch (verbosity) {
    case 0:
        flags = 0;
        break;
    case 1:
        flags = LOG_CPERROR;
        break;
    case 2:
        flags = LOG_CPERROR | LOG_CPWARNING;
        break;
    case 3:
        flags = LOG_CPERROR | LOG_CPWARNING | LOG_CPINFO;
        break;
    default:
        flags = LOG_CPERROR | LOG_CPWARNING | LOG_CPINFO | LOG_CPDEBUG;
        break;
    }
    return flags;
}

static int parse_args(int argc, char **argv, int *exitcode)
{
    int i;
    int log_stderr = LOG_CPERROR;
    int log_flags = LOG_CPERROR | LOG_CPWARNING | LOG_CPINFO;

    for (i = 1; i != argc; ++i) {
        char *argi = argv[i];
        if (argi[0] != '-') {
            luafile = argi;
        }
        else if (argi[1] == '-') {     /* long format */
            if (strcmp(argi + 2, "version") == 0) {
                printf("\n%s PBEM host\n"
                    "Copyright (C) 1996-2005 C. Schlittchen, K. Zedel, E. Rehling, H. Peters.\n\n"
                    "Compilation: " __DATE__ " at " __TIME__ "\nVersion: %s\n\n",
                    game_name(), eressea_version());
#ifdef USE_CURSES          
            }
            else if (strcmp(argi + 2, "color") == 0) {
                /* force the editor to have colors */
                force_color = 1;
#endif          
            }
            else if (strcmp(argi + 2, "help") == 0) {
                return usage(argv[0], NULL);
            }
            else {
                return usage(argv[0], argi);
            }
        }
        else {
            const char *arg;
            switch (argi[1]) {
            case 'r':
                i = get_arg(argc, argv, 2, i, &arg, 0);
                config_set("config.rules", arg);
                break;
            case 'f':
                i = get_arg(argc, argv, 2, i, &luafile, 0);
                break;
            case 'l':
                i = get_arg(argc, argv, 2, i, &arg, 0);
                log_flags = arg ? atoi(arg) : 0xff;
                break;
            case 't':
                i = get_arg(argc, argv, 2, i, &arg, 0);
                turn = atoi(arg);
                break;
            case 'q':
                verbosity = 0;
                break;
            case 'v':
                i = get_arg(argc, argv, 2, i, &arg, 0);
                verbosity = arg ? atoi(arg) : 0xff;
                break;
            case 'h':
                usage(argv[0], NULL);
                return 1;
            default:
                *exitcode = -1;
                usage(argv[0], argi);
                return 1;
            }
        }
    }

    // open logfile on disk:
    log_flags = verbosity_to_flags(log_flags);
    log_open(logfile, log_flags);

    // also log to stderr:
    log_stderr = verbosity_to_flags(verbosity);
    if (log_stderr) {
        log_to_file(log_stderr | LOG_FLUSH | LOG_BRIEF, stderr);
    }
    return 0;
}

#if defined(HAVE_SIGACTION) && defined(HAVE_EXECINFO)
#include <execinfo.h>
#include <signal.h>

static void report_segfault(int signo, siginfo_t * sinf, void *arg)
{
    void *btrace[50];
    size_t size;
    int fd = fileno(stderr);

    fflush(stdout);
    fputs("\n\nProgram received SIGSEGV, backtrace follows.\n", stderr);
    size = backtrace(btrace, 50);
    backtrace_symbols_fd(btrace, size, fd);
    abort();
}

static int setup_signal_handler(void)
{
    struct sigaction act;

    act.sa_flags = SA_ONESHOT | SA_SIGINFO;
    act.sa_sigaction = report_segfault;
    sigfillset(&act.sa_mask);
    return sigaction(SIGSEGV, &act, NULL);
}
#else
static int setup_signal_handler(void)
{
    return 0;
}
#endif

void locale_init(void)
{
    setlocale(LC_CTYPE, "");
    setlocale(LC_NUMERIC, "C");
    if (towlower(0xC4) != 0xE4) { /* &Auml; => &auml; */
        log_error("Umlaut conversion is not working properly. Wrong locale? LANG=%s\n",
            getenv("LANG"));
    }
}

extern void bind_monsters(struct lua_State *L);

int main(int argc, char **argv)
{
    int err = 0;
    lua_State *L;
    setup_signal_handler();
    /* ini file sets defaults for arguments*/
    parse_config(inifile);
    if (!global.inifile) {
        log_error("could not open ini configuration %s\n", inifile);
    }
    /* parse arguments again, to override ini file */
    parse_args(argc, argv, &err);

    locale_init();

    L = lua_init();
    game_init();
    bind_monsters(L);
    err = eressea_run(L, luafile);
    if (err) {
        log_error("script %s failed with code %d\n", luafile, err);
        return err;
    }
    game_done();
    lua_done(L);
    log_close();
    if (global.inifile) {
        iniparser_freedict(global.inifile);
    }
    return 0;
}
