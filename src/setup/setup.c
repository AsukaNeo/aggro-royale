/*
 * aggro_setup.exe: makes the version of Aggro Royale the player wants from
 * the games they own. Aggressive Inline is needed (set up as the game
 * itself does on first run, game_import.c); Dave Mirra Freestyle BMX 2 and
 * BMX XXX, each optional, add their levels to Free Skate and their music as
 * a soundtrack -- mods built from the player's own copies (zaxis.c,
 * level_menu.c). Leaving a game out removes what it added. Nothing of any
 * game is changed, and nothing of theirs comes with this program.
 *
 * Without arguments it is a window. For scripts:
 *   aggro_setup --build [--dm2 <game>] [--xxx <game>] [--no-dm2] [--no-xxx]
 *                       [--ai <game>] [--log <file>]
 *   aggro_setup --port <game> <dm2|xxx> <level> <mod folder> <name> <AI folder>
 *   aggro_setup --menu <mods folder> <AI folder>
 *   aggro_setup --shot <file.bmp>       (the window, saved, then closed)
 * where <game> is a folder with default.xbe, a default.xbe or a disc image.
 * A game left unnamed in --build keeps what it added before.
 */
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game_check.h"
#include "../game_import.h"
#include "setup_src.h"
#include "zaxis.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
                        "language='*'\"")

#define TITLE AR_APP " setup"

/* ---- what each game adds -------------------------------------------------- */

typedef struct {
    const char *file, *title, *artist;
} Song;

typedef struct {
    const char *folder, *name;
} AddedLevel;

typedef struct {
    const char       *tag;              /* mod folders <tag>-levels, <tag>-music */
    const char       *name;
    uint32_t          title_id;
    const char       *soundtrack;       /* as the music menu names it */
    const Song       *songs;
    int               nsongs;
    const AddedLevel *levels;
    int               nlevels;
} Game;

static const Song DM2_SONGS[] = {
    { "Wakeup.wma",             "Wake Up",             "Rage Against the Machine" },
    { "Paranoid_nointro.wma",   "Paranoid",            "Black Sabbath" },
    { "Dointime.wma",           "Doin' Time",          "Sublime" },
    { "Makesnodifference.wma",  "Makes No Difference", "Sum 41" },
    { "SheSellsSanctuary.wma",  "She Sells Sanctuary", "The Cult" },
    { "Buginoutedit.wma",       "Buggin' Out",         "A Tribe Called Quest" },
    { "Hypocritical.wma",       "Hypocritical",        "Methods of Mayhem" },
    { "Awake.wma",              "Awake",               "Godsmack" },
    { "AllmyFault.wma",         "All My Fault",        "Fenix TX" },
    { "Momentoftruthradio.wma", "Moment of Truth",     "Gang Starr" },
};

static const Song XXX_SONGS[] = {
    { "DateRape.WMA",           "Date Rape",                "Sublime" },
    { "Down.WMA",               "Down",                     "311" },
    { "GirlsGirlsGirls.WMA",    "Girls Girls Girls",        "Motley Crue" },
    { "ClickClickBoom.WMA",     "Click Click Boom",         "Saliva" },
    { "Lapdance.WMA",           "Lapdance",                 "N.E.R.D." },
    { "MagicNumber.WMA",        "The Magic Number",         "De La Soul" },
    { "Juice.WMA",              "Juice (Know The Ledge)",   "Eric B. and Rakim" },
    { "RapturesDelight.WMA",    "Step Into a World",        "KRS-One" },
    { "BasketCase.WMA",         "Basket Case",              "Green Day" },
    { "MyFriendsOverYou.WMA",   "My Friends Over You",      "New Found Glory" },
    { "Downfall.WMA",           "Downfall",                 "Trust Company" },
    { "TheEnergy.WMA",          "The Energy",               "Audiovent" },
    { "Plastic.WMA",            "Plastic",                  "Ten Foot Pole" },
    { "GivingGravityAHand.WMA", "Giving Gravity a Hand",    "Ten Foot Pole" },
    { "CuteWithoutTheE.WMA",    "Cute without the E",       "Taking Back Sunday" },
    { "JudgmentDay.WMA",        "Judgement Day",            "Brand New Sin" },
    { "SPP.WMA",                "SPP",                      "Brand New Sin" },
    { "Crackling.WMA",          "Crackling",                "Alien Breed" },
    { "EvilTwin.WMA",           "Evil Twin",                "Alien Breed" },
    { "BoutThatTime.WMA",       "Bout that Time",           "Large Professor" },
    { "Quarashi.WMA",           "Stick'em Up",              "Quarashi" },
    { "JustaGirl.WMA",          "Just A Girl",              "No Doubt" },
    { "LooksThatKill.WMA",      "Looks That Kill",          "Motley Crue" },
    { "MotleyCrue.WMA",         "Wild Side",                "Motley Crue" },
    { "NERD.WMA",               "Project Pluto",            "N.E.R.D." },
    { "WhiteStripes.WMA",       "Fell in Love With a Girl", "The White Stripes" },
    { "3IsTheMagicNumber.WMA",  "3 Is The Magic Number",    "" },
    { "KnowtheLedge.WMA",       "Know The Ledge",           "Eric B. and Rakim" },
};

static const AddedLevel DM2_LEVELS[] = {
    { "WOODWARD", "Woodward" }, { "AIRPORT", "Airport" }, { "CLOVERLE", "Cloverleaf" },
    { "COMMERCI", "Commercial" }, { "DEVILSPE", "Devils Peak" }, { "GALLOON", "Galloon" },
    { "GREENVIL", "Greenville" }, { "SWAMPYTR", "Swamp Trail" }, { "TRAINYAR", "Train Yard" },
    { "Venice", "Venice Beach" },
};

static const AddedLevel XXX_LEVELS[] = {
    { "CMP_RMPG", "Rampage Park" }, { "CHL_DAM", "The Dam" }, { "CHL_MNCO", "Mnco" },
    { "CHL_MULY", "Mullaly" }, { "CHL_NASA", "Launch Pad" }, { "CHL_SHEP", "Sheep Farm" },
    { "CHL_SYRA", "Syracuse" }, { "CMP_ROOT", "Root" },
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const Game GAMES[2] = {
    { "dm2", "Dave Mirra Freestyle BMX 2", 0x41430001u, "Dave Mirra 2",
      DM2_SONGS, COUNT(DM2_SONGS), DM2_LEVELS, COUNT(DM2_LEVELS) },
    { "xxx", "BMX XXX", 0x4143000Eu, "BMX XXX",
      XXX_SONGS, COUNT(XXX_SONGS), XXX_LEVELS, COUNT(XXX_LEVELS) },
};

#define AI_TITLE_ID 0x41430007u

/* ---- the log ---------------------------------------------------------------- */

static HWND  s_wnd;              /* the window, when there is one */
static FILE *s_logf;
#define WM_SETUP_LOG  (WM_APP + 1)
#define WM_SETUP_DONE (WM_APP + 2)

void setup_log(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (s_logf) {
        fprintf(s_logf, "%s\n", line);
        fflush(s_logf);
    }
    if (s_wnd)
        PostMessageA(s_wnd, WM_SETUP_LOG, 0, (LPARAM)_strdup(line));
}

/* ---- where things go --------------------------------------------------------- */

static char s_exe_dir[MAX_PATH];

static int game_ok(const char *dir)
{
    char path[MAX_PATH], found[41];
    size_t size;
    void *xbe;
    int ok;
    if (!dir || !*dir)
        return 0;
    snprintf(path, sizeof path, "%s\\default.xbe", dir);
    xbe = zx_read_file(path, &size);
    ok = xbe && ar_game_check(xbe, size, found);
    free(xbe);
    return ok;
}

/* The game folder the game itself would use (main.c, ar_find_game_dir). */
static int find_game_dir(char *out, size_t n)
{
    char cand[MAX_PATH];
    const char *env = getenv("RECOMP_GAME_DIR");
    if (env && game_ok(env))
        return GetFullPathNameA(env, (DWORD)n, out, NULL) != 0;
    ar_game_import_remembered(s_exe_dir, cand, sizeof cand);
    if (game_ok(cand))
        return GetFullPathNameA(cand, (DWORD)n, out, NULL) != 0;
    snprintf(cand, sizeof cand, "%s\\game", s_exe_dir);
    if (game_ok(cand))
        return GetFullPathNameA(cand, (DWORD)n, out, NULL) != 0;
    snprintf(cand, sizeof cand, "%s\\..\\..\\game", s_exe_dir);
    if (game_ok(cand))
        return GetFullPathNameA(cand, (DWORD)n, out, NULL) != 0;
    return 0;
}

/* The mods folder the game reads (main.c, ar_mods): beside a folder named
 * "game", else beside the exe. */
static void mods_dir(const char *game_dir, char *out, size_t n)
{
    char g[MAX_PATH], *slash;
    snprintf(g, sizeof g, "%s", game_dir);
    if ((slash = strrchr(g, '\\')) != NULL && !_stricmp(slash + 1, "game")) {
        *slash = 0;
        snprintf(out, n, "%s\\mods", g);
    } else
        snprintf(out, n, "%s\\mods", s_exe_dir);
}

static int mods_present(const char *mods, const Game *g)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%s-levels\\levels.txt", mods, g->tag);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
        return 1;
    snprintf(path, sizeof path, "%s\\%s-music\\soundtrack.txt", mods, g->tag);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void remove_mods(const char *mods, const Game *g)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%s-levels", mods, g->tag);
    zx_remove_tree(path);
    snprintf(path, sizeof path, "%s\\%s-music", mods, g->tag);
    zx_remove_tree(path);
}

/* ---- building ----------------------------------------------------------------- */

static int build_music(ArSrc *src, const Game *g, const char *mods)
{
    char dir[MAX_PATH], path[MAX_PATH], rel[MAX_PATH];
    FILE *f;
    int i, copied = 0;

    snprintf(dir, sizeof dir, "%s\\%s-music\\songs", mods, g->tag);
    if (!zx_make_dirs(dir)) {
        setup_log("  Could not create %s.", dir);
        return 0;
    }
    snprintf(path, sizeof path, "%s\\%s-music\\soundtrack.txt", mods, g->tag);
    f = fopen(path, "w");
    if (!f) {
        setup_log("  Could not write %s.", path);
        return 0;
    }
    fprintf(f, "# A soundtrack for Aggro Royale: songs in this mod's folder, one per line,\n"
               "#   <file> | <title> | <artist>\n"
               "# Made by aggro_setup from your own copy of %s.\n"
               "name: %s\n", g->name, g->soundtrack);
    for (i = 0; i < g->nsongs; i++) {
        size_t size;
        void *data;
        snprintf(rel, sizeof rel, "media/assets/audio/%s", g->songs[i].file);
        data = ar_src_read(src, rel, &size);
        snprintf(path, sizeof path, "%s\\%s", dir, g->songs[i].file);
        if (!data || !zx_write_file(path, data, size)) {
            setup_log("  %s: not found in the game, left out", g->songs[i].title);
            free(data);
            continue;
        }
        free(data);
        fprintf(f, "songs/%s | %s | %s\n", g->songs[i].file, g->songs[i].title, g->songs[i].artist);
        copied++;
    }
    fclose(f);
    setup_log("  Soundtrack \"%s\": %d songs", g->soundtrack, copied);
    return copied > 0;
}

static int build_game(const Game *g, const char *source, const char *mods, const char *ai)
{
    char err[512], mod[MAX_PATH];
    ArSrc *src;
    int i, ok = 0;
    uint32_t id;

    setup_log("%s", g->name);
    src = ar_src_open(source, err, sizeof err);
    if (!src) {
        setup_log("  %s", err);
        return 0;
    }
    id = ar_src_title_id(src);
    if (id != g->title_id) {
        setup_log("  This is not %s (its title ID is %08X).", g->name, (unsigned)id);
        ar_src_close(src);
        return 0;
    }
    remove_mods(mods, g);
    build_music(src, g, mods);
    snprintf(mod, sizeof mod, "%s\\%s-levels", mods, g->tag);
    for (i = 0; i < g->nlevels; i++) {
        setup_log("  Level %d of %d: %s", i + 1, g->nlevels, g->levels[i].name);
        if (zx_port_level(src, g->tag, g->levels[i].folder, mod, g->levels[i].name, ai,
                          err, sizeof err))
            ok++;
        else
            setup_log("    left out: %s", err);
    }
    ar_src_close(src);
    return ok > 0;
}

typedef struct {
    char ai[MAX_PATH];                 /* a new Aggressive Inline, or "" */
    char source[2][MAX_PATH];          /* "" keeps what was added before */
    int  include[2];
} Plan;

/* 1 if everything asked for was done. */
static int run_plan(const Plan *p)
{
    char game[MAX_PATH], mods[MAX_PATH], err[512];
    int i, ok = 1, n;

    if (p->ai[0]) {
        char from[MAX_PATH], dir[MAX_PATH];
        DWORD attr = GetFileAttributesA(p->ai);
        snprintf(from, sizeof from, "%s", p->ai);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            strncat(from, "\\default.xbe", sizeof from - strlen(from) - 1);
        setup_log("Aggressive Inline");
        if (!ar_game_import(s_exe_dir, from, dir, sizeof dir)) {
            setup_log("  Not set up.");
            return 0;
        }
        setup_log("  Ready: %s", dir);
    }
    if (!find_game_dir(game, sizeof game)) {
        setup_log("Aggressive Inline is not set up yet: choose your copy of it first.");
        return 0;
    }
    mods_dir(game, mods, sizeof mods);
    zx_make_dirs(mods);
    for (i = 0; i < 2; i++) {
        const Game *g = &GAMES[i];
        if (!p->include[i]) {
            if (mods_present(mods, g))
                setup_log("%s: removed", g->name);
            remove_mods(mods, g);
        } else if (p->source[i][0])
            ok &= build_game(g, p->source[i], mods, game);
        else if (mods_present(mods, g))
            setup_log("%s: kept as it was", g->name);
    }
    setup_log("Free Skate");
    n = zx_build_level_menu(mods, game, err, sizeof err);
    if (n < 0) {
        setup_log("  %s", err);
        ok = 0;
    } else if (n == 0)
        setup_log("  No added levels.");
    setup_log(ok ? "Done. Start Aggro Royale to play." : "Finished, with problems (see above).");
    return ok;
}

/* ---- finding the games ----------------------------------------------------------- */

/* What `path` is: a game we know, by title ID, or 0. */
static uint32_t identify(const char *path)
{
    char err[256];
    ArSrc *s = ar_src_open(path, err, sizeof err);
    uint32_t id = s ? ar_src_title_id(s) : 0;
    ar_src_close(s);
    return id;
}

/* The games beside the setup and a few folders up: folders with a
 * default.xbe and disc images. */
static void scan(char found[3][MAX_PATH])
{
    char dir[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    int up;

    snprintf(dir, sizeof dir, "%s", s_exe_dir);
    for (up = 0; up < 4; up++) {
        WIN32_FIND_DATAA fd;
        HANDLE h;
        char *slash;
        snprintf(pattern, sizeof pattern, "%s\\*", dir);
        h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            int seen = 0;
            do {
                const char *dot = strrchr(fd.cFileName, '.');
                int dir_ = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                uint32_t id;
                if (fd.cFileName[0] == '.' || ++seen > 400)
                    continue;
                if (!dir_ && !(dot && (!_stricmp(dot, ".iso") || !_stricmp(dot, ".xiso"))))
                    continue;
                snprintf(path, sizeof path, "%s\\%s", dir, fd.cFileName);
                if (dir_) {
                    char xbe[MAX_PATH];
                    snprintf(xbe, sizeof xbe, "%s\\default.xbe", path);
                    if (GetFileAttributesA(xbe) == INVALID_FILE_ATTRIBUTES)
                        continue;
                }
                id = identify(path);
                if (id == AI_TITLE_ID && !found[0][0])
                    snprintf(found[0], MAX_PATH, "%s", path);
                if (id == GAMES[0].title_id && !found[1][0])
                    snprintf(found[1], MAX_PATH, "%s", path);
                if (id == GAMES[1].title_id && !found[2][0])
                    snprintf(found[2], MAX_PATH, "%s", path);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        if ((slash = strrchr(dir, '\\')) == NULL || slash - dir < 3)
            break;
        *slash = 0;
    }
}

/* ---- the window ---------------------------------------------------------------------- */

enum {
    ID_PATH = 100,                     /* + row */
    ID_FOLDER = 110,
    ID_IMAGE = 120,
    ID_CHECK = 130,
    ID_STATUS = 140,
    ID_BUILD = 200,
    ID_PLAY,
    ID_LOG,
};

static HWND s_path[3], s_folder[3], s_image[3], s_check[3], s_status[3], s_build, s_play, s_log;
static HFONT s_font, s_bold;
static int s_busy;
static char s_ready_game[MAX_PATH];    /* Aggressive Inline, already set up */

static int dpi(int v)
{
    static int scale;
    if (!scale) {
        HDC dc = GetDC(NULL);
        scale = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    return MulDiv(v, scale, 96);
}

static HWND child(HWND parent, const char *cls, const char *text, DWORD style, int x, int y,
                  int w, int h, int id)
{
    HWND c = CreateWindowExA(!strcmp(cls, "EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text,
                             WS_CHILD | WS_VISIBLE | style, dpi(x), dpi(y), dpi(w), dpi(h), parent,
                             (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)s_font, TRUE);
    return c;
}

static void row_status(int row)
{
    char path[MAX_PATH], text[512];
    uint32_t want = row == 0 ? AI_TITLE_ID : GAMES[row - 1].title_id, id;
    const char *name = row == 0 ? "Aggressive Inline" : GAMES[row - 1].name;

    GetWindowTextA(s_path[row], path, sizeof path);
    if (row > 0 && SendMessageA(s_check[row], BM_GETCHECK, 0, 0) != BST_CHECKED) {
        char mods[MAX_PATH];
        int had = s_ready_game[0] && (mods_dir(s_ready_game, mods, sizeof mods),
                                      mods_present(mods, &GAMES[row - 1]));
        snprintf(text, sizeof text, had ? "Not included: what it added will be removed."
                                        : "Not included.");
    } else if (!path[0]) {
        char mods[MAX_PATH];
        if (row == 0)
            snprintf(text, sizeof text, "Choose the folder or disc image of your copy.");
        else if (s_ready_game[0] && (mods_dir(s_ready_game, mods, sizeof mods),
                                     mods_present(mods, &GAMES[row - 1])))
            snprintf(text, sizeof text, "Added before; kept as it is. Choose your copy to add it again.");
        else
            snprintf(text, sizeof text, "Choose the folder or disc image of your copy.");
    } else if (row == 0 && s_ready_game[0] && !_stricmp(path, s_ready_game))
        snprintf(text, sizeof text, "Ready: Aggro Royale is set up with this copy.");
    else if ((id = identify(path)) == want)
        snprintf(text, sizeof text, row == 0 ? "Found %s: it will be set up." :
                 "Found %s: its levels and music will be added.", name);
    else if (id)
        snprintf(text, sizeof text, "This is a different game (title %08X), not %s.", (unsigned)id, name);
    else
        snprintf(text, sizeof text, "No Xbox game here: choose the folder with default.xbe in it, "
                 "or a disc image.");
    SetWindowTextA(s_status[row], text);
}

static int pick_folder(HWND owner, char *out, size_t n)
{
    IFileOpenDialog *dlg;
    IShellItem *item;
    PWSTR wide;
    int ok = 0;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&dlg)))
        return 0;
    IFileOpenDialog_SetOptions(dlg, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    IFileOpenDialog_SetTitle(dlg, L"Choose the game's folder (the one with default.xbe in it)");
    if (SUCCEEDED(IFileOpenDialog_Show(dlg, owner)) && SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item))) {
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wide))) {
            ok = WideCharToMultiByte(CP_ACP, 0, wide, -1, out, (int)n, NULL, NULL) > 0;
            CoTaskMemFree(wide);
        }
        IShellItem_Release(item);
    }
    IFileOpenDialog_Release(dlg);
    return ok;
}

static int pick_image(HWND owner, char *out, size_t n)
{
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof ofn);
    out[0] = 0;
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "Xbox disc image\0*.iso;*.xiso\0All files\0*.*\0";
    ofn.lpstrFile = out;
    ofn.nMaxFile = (DWORD)n;
    ofn.lpstrTitle = "Choose the game's disc image";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    return GetOpenFileNameA(&ofn) != 0;
}

static DWORD WINAPI worker(LPVOID arg)
{
    Plan *p = (Plan *)arg;
    int ok;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok = run_plan(p);
    free(p);
    PostMessageA(s_wnd, WM_SETUP_DONE, (WPARAM)ok, 0);
    CoUninitialize();
    return 0;
}

static void set_busy(int busy)
{
    int i;
    s_busy = busy;
    for (i = 0; i < 3; i++) {
        EnableWindow(s_path[i], !busy);
        EnableWindow(s_folder[i], !busy);
        EnableWindow(s_image[i], !busy);
        if (s_check[i])
            EnableWindow(s_check[i], !busy);
    }
    EnableWindow(s_build, !busy);
    EnableWindow(s_play, !busy && s_ready_game[0]);
}

static void start_build(void)
{
    Plan *p = (Plan *)calloc(1, sizeof *p);
    char path[MAX_PATH];
    int i;
    if (!p)
        return;
    GetWindowTextA(s_path[0], path, sizeof path);
    if (path[0] && (!s_ready_game[0] || _stricmp(path, s_ready_game)))
        snprintf(p->ai, sizeof p->ai, "%s", path);
    if (!p->ai[0] && !s_ready_game[0]) {
        MessageBoxA(s_wnd, "Choose your copy of Aggressive Inline first: Aggro Royale is built on it.",
                    TITLE, MB_ICONINFORMATION);
        free(p);
        return;
    }
    for (i = 0; i < 2; i++) {
        p->include[i] = SendMessageA(s_check[i + 1], BM_GETCHECK, 0, 0) == BST_CHECKED;
        GetWindowTextA(s_path[i + 1], p->source[i], MAX_PATH);
    }
    SetWindowTextA(s_log, "");
    set_busy(1);
    CloseHandle(CreateThread(NULL, 0, worker, p, 0, NULL));
}

static void append_log(const char *line)
{
    int len = GetWindowTextLengthA(s_log);
    SendMessageA(s_log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(s_log, EM_REPLACESEL, FALSE, (LPARAM)line);
    SendMessageA(s_log, EM_REPLACESEL, FALSE, (LPARAM)"\r\n");
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w), row;
        char path[MAX_PATH];
        if (id >= ID_FOLDER && id < ID_FOLDER + 3 && pick_folder(h, path, sizeof path)) {
            row = id - ID_FOLDER;
            SetWindowTextA(s_path[row], path);
            if (s_check[row])
                SendMessageA(s_check[row], BM_SETCHECK, BST_CHECKED, 0);
        } else if (id >= ID_IMAGE && id < ID_IMAGE + 3 && pick_image(h, path, sizeof path)) {
            row = id - ID_IMAGE;
            SetWindowTextA(s_path[row], path);
            if (s_check[row])
                SendMessageA(s_check[row], BM_SETCHECK, BST_CHECKED, 0);
        } else if (id >= ID_CHECK && id < ID_CHECK + 3)
            row_status(id - ID_CHECK);
        else if (id >= ID_PATH && id < ID_PATH + 3 && HIWORD(w) == EN_KILLFOCUS)
            row_status(id - ID_PATH);
        else if (id == ID_BUILD && !s_busy)
            start_build();
        else if (id == ID_PLAY) {
            char exe[MAX_PATH];
            snprintf(exe, sizeof exe, "%s\\aggro_royale.exe", s_exe_dir);
            if ((INT_PTR)ShellExecuteA(h, "open", exe, NULL, s_exe_dir, SW_SHOWNORMAL) > 32)
                PostMessageA(h, WM_CLOSE, 0, 0);
            else
                MessageBoxA(h, "Could not start aggro_royale.exe: it should be next to this setup.",
                            TITLE, MB_ICONERROR);
        }
        if (id >= ID_FOLDER && id < ID_IMAGE + 3)
            row_status(id >= ID_IMAGE ? id - ID_IMAGE : id - ID_FOLDER);
        return 0;
    }
    case WM_SETUP_LOG:
        append_log((const char *)l);
        free((void *)l);
        return 0;
    case WM_SETUP_DONE: {
        char game[MAX_PATH];
        int i;
        if (find_game_dir(game, sizeof game)) {
            snprintf(s_ready_game, sizeof s_ready_game, "%s", game);
            SetWindowTextA(s_path[0], game);
        }
        for (i = 1; i < 3; i++)
            SetWindowTextA(s_path[i], "");
        set_busy(0);
        for (i = 0; i < 3; i++)
            row_status(i);
        MessageBoxA(h, w ? "Done. Aggro Royale is ready to play." :
                    "Finished, but not everything worked: see the list in the window.",
                    TITLE, w ? MB_ICONINFORMATION : MB_ICONWARNING);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        if (s_busy && MessageBoxA(h, "The setup is still working. Stop it and close?", TITLE,
                                  MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void make_window(void)
{
    static const char *const names[3] = { "Aggressive Inline (needed)",
                                          "Dave Mirra Freestyle BMX 2", "BMX XXX" };
    NONCLIENTMETRICSA ncm;
    WNDCLASSA wc;
    RECT r = { 0, 0, 0, 0 };
    char found[3][MAX_PATH];
    char game[MAX_PATH], mods[MAX_PATH];
    int i, y;

    ncm.cbSize = sizeof ncm;
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    s_font = CreateFontIndirectA(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_BOLD;
    s_bold = CreateFontIndirectA(&ncm.lfMessageFont);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "AggroRoyaleSetup";
    RegisterClassA(&wc);
    r.right = dpi(640);
    r.bottom = dpi(560);
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    s_wnd = CreateWindowA(wc.lpszClassName, TITLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                          CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                          NULL, NULL, wc.hInstance, NULL);

    child(s_wnd, "STATIC",
          "Aggro Royale is built from your own copies of the games. Aggressive Inline is needed. "
          "Dave Mirra Freestyle BMX 2 and BMX XXX are optional: each adds its levels to Free Skate "
          "and its music as a soundtrack. Untick one to remove what it added.",
          SS_LEFT, 16, 12, 608, 52, 0);
    memset(found, 0, sizeof found);
    scan(found);
    if (find_game_dir(game, sizeof game))
        snprintf(s_ready_game, sizeof s_ready_game, "%s", game);
    y = 72;
    for (i = 0; i < 3; i++, y += 92) {
        HWND title;
        if (i == 0)
            title = child(s_wnd, "STATIC", names[i], SS_LEFT, 16, y, 400, 20, 0);
        else
            title = s_check[i] = child(s_wnd, "BUTTON", names[i], BS_AUTOCHECKBOX, 16, y, 400, 20,
                                       ID_CHECK + i);
        SendMessageA(title, WM_SETFONT, (WPARAM)s_bold, TRUE);
        s_path[i] = child(s_wnd, "EDIT", "", ES_AUTOHSCROLL, 16, y + 24, 388, 24, ID_PATH + i);
        s_folder[i] = child(s_wnd, "BUTTON", "Folder...", BS_PUSHBUTTON, 412, y + 23, 100, 26, ID_FOLDER + i);
        s_image[i] = child(s_wnd, "BUTTON", "Disc image...", BS_PUSHBUTTON, 518, y + 23, 106, 26, ID_IMAGE + i);
        s_status[i] = child(s_wnd, "STATIC", "", SS_LEFT | SS_ENDELLIPSIS, 16, y + 54, 608, 20, ID_STATUS + i);
    }
    SetWindowTextA(s_path[0], s_ready_game[0] ? s_ready_game : found[0]);
    for (i = 1; i < 3; i++) {
        int had = s_ready_game[0] && (mods_dir(s_ready_game, mods, sizeof mods),
                                      mods_present(mods, &GAMES[i - 1]));
        SetWindowTextA(s_path[i], found[i]);
        SendMessageA(s_check[i], BM_SETCHECK, (found[i][0] || had) ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    for (i = 0; i < 3; i++)
        row_status(i);
    s_build = child(s_wnd, "BUTTON", "Build", BS_DEFPUSHBUTTON, 16, y, 140, 30, ID_BUILD);
    s_play = child(s_wnd, "BUTTON", "Play", BS_PUSHBUTTON, 164, y, 140, 30, ID_PLAY);
    EnableWindow(s_play, s_ready_game[0] != 0);
    s_log = child(s_wnd, "EDIT", "", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                  16, y + 40, 608, 560 - y - 52, ID_LOG);
    ShowWindow(s_wnd, SW_SHOWNORMAL);
    UpdateWindow(s_wnd);
}

/* ---- start -------------------------------------------------------------------------- */

static int cli(int argc, char **argv)
{
    Plan plan;
    int i, ok;

    memset(&plan, 0, sizeof plan);
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--log") && i + 1 < argc) {
            s_logf = fopen(argv[i + 1], "w");
            break;
        }
    if (!s_logf) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\aggro_setup.log", s_exe_dir);
        s_logf = fopen(path, "w");
    }
    if (!strcmp(argv[1], "--port") && argc >= 8) {
        char err[512];
        ArSrc *src = ar_src_open(argv[2], err, sizeof err);
        ok = src && zx_port_level(src, argv[3], argv[4], argv[5], argv[6], argv[7], err, sizeof err);
        if (!ok)
            setup_log("%s", err);
        ar_src_close(src);
        return ok ? 0 : 1;
    }
    if (!strcmp(argv[1], "--menu") && argc >= 4) {
        char err[512];
        int n = zx_build_level_menu(argv[2], argv[3], err, sizeof err);
        if (n < 0)
            setup_log("%s", err);
        return n < 0;
    }
    if (strcmp(argv[1], "--build")) {
        setup_log("usage: aggro_setup [--build [--ai X] [--dm2 X] [--xxx X] [--no-dm2] [--no-xxx]]");
        return 2;
    }
    plan.include[0] = plan.include[1] = 1;
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--ai") && i + 1 < argc)
            snprintf(plan.ai, sizeof plan.ai, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--dm2") && i + 1 < argc)
            snprintf(plan.source[0], MAX_PATH, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--xxx") && i + 1 < argc)
            snprintf(plan.source[1], MAX_PATH, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--no-dm2"))
            plan.include[0] = 0;
        else if (!strcmp(argv[i], "--no-xxx"))
            plan.include[1] = 0;
        else if (!strcmp(argv[i], "--log"))
            i++;
    }
    return run_plan(&plan) ? 0 : 1;
}

/* --shot <file.bmp>: the window as it opens, saved, for checking its layout. */
static int shot(const char *path)
{
    RECT r;
    HDC screen, dc;
    HBITMAP bmp;
    BITMAPINFOHEADER bi;
    BITMAPFILEHEADER bf;
    uint8_t *px;
    FILE *f;
    int w, h, ok = 0;
    DWORD t0 = GetTickCount();
    MSG msg;

    make_window();
    SetWindowPos(s_wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    while (GetTickCount() - t0 < 1000)
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    /* The visible frame: GetWindowRect also takes in the invisible resize border. */
    if (FAILED(DwmGetWindowAttribute(s_wnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r)))
        GetWindowRect(s_wnd, &r);
    w = r.right - r.left;
    h = r.bottom - r.top;
    screen = GetDC(NULL);
    dc = CreateCompatibleDC(screen);
    bmp = CreateCompatibleBitmap(screen, w, h);
    SelectObject(dc, bmp);
    /* From the screen: PrintWindow left controls unpainted. */
    RedrawWindow(s_wnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    BitBlt(dc, 0, 0, w, h, screen, r.left, r.top, SRCCOPY);
    memset(&bi, 0, sizeof bi);
    bi.biSize = sizeof bi;
    bi.biWidth = w;
    bi.biHeight = h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    px = (uint8_t *)malloc((size_t)w * h * 4);
    if (px && GetDIBits(dc, bmp, 0, (UINT)h, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS)
        && (f = fopen(path, "wb")) != NULL) {
        memset(&bf, 0, sizeof bf);
        bf.bfType = 0x4D42;
        bf.bfOffBits = sizeof bf + sizeof bi;
        bf.bfSize = bf.bfOffBits + (DWORD)w * h * 4;
        ok = fwrite(&bf, sizeof bf, 1, f) && fwrite(&bi, sizeof bi, 1, f)
             && fwrite(px, (size_t)w * h * 4, 1, f);
        fclose(f);
    }
    free(px);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    DestroyWindow(s_wnd);
    return ok ? 0 : 1;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    MSG msg;
    char *slash;
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS };
    (void)inst; (void)prev; (void)cmd; (void)show;

    SetProcessDPIAware();
    GetModuleFileNameA(NULL, s_exe_dir, MAX_PATH);
    if ((slash = strrchr(s_exe_dir, '\\')) != NULL)
        *slash = 0;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitCommonControlsEx(&icc);
    if (__argc > 2 && !strcmp(__argv[1], "--shot"))
        return shot(__argv[2]);
    if (__argc > 1)
        return cli(__argc, __argv);
    make_window();
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(s_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    return 0;
}
