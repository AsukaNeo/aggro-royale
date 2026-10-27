/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

#include "gen/recomp_types.h"   /* thread-local register model (replaces a plain extern) */
__declspec(dllimport) unsigned long __stdcall GetCurrentThreadId(void);
extern ptrdiff_t g_xbox_mem_offset;


/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[NULLCALL] ret=%08X arg0=%08X eax=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebx=%08X"
                    " esp=%08X\n", (unsigned)MEM32(g_esp), (unsigned)MEM32(g_esp+4), (unsigned)g_eax,
            (unsigned)g_ecx, (unsigned)g_edx, (unsigned)g_esi, (unsigned)g_edi, (unsigned)g_ebx, (unsigned)g_esp);
    { uint32_t o = g_esi - 16; int q; if (o < 0x08000000u) { fprintf(stderr, "[NULLOBJ] esi-16:");
      for (q = 0; q < 80; q++) fprintf(stderr, "%s%02X", (q%16==0)?"\n  ":" ", (unsigned)MEM8(o+q));
      fprintf(stderr, "\n"); } }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

int xbox_gdlock_insn(const char *text, uint32_t va);   /* gd1 */
void recomp_unimpl(const char *text, uint32_t va)
{
    if (xbox_gdlock_insn(text, va)) return;   /* gd1: cli/sti */
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}

/* ── Aggro Royale overrides ─────────────────────────────────── */

/* sub_00089F20: the game's hand-written memcpy(dst, src, n), cdecl, returns
 * dst. It copies the 0-7 bytes before an aligned run (and the 0-15 dwords
 * after it) by computing an address inside a run of movsb/movsd and jumping
 * there (`add ecx, 0x89F6C; jmp ecx`). The lifter can only send `jmp ecx`
 * through the dispatch table, and those addresses are not functions, so every
 * copy whose length or alignment needed the run went wrong: "Failed to
 * resolve VA" at 0x00089F67/69/6C and 0x0008A0DD/DE/E3, then a bad pointer.
 * Done natively instead. memmove, since the original's forward copy is only
 * ever handed non-overlapping buffers and memmove is safe either way. */
void sub_00089F20(void)
{
    uint32_t dst = MEM32(g_esp + 4);
    uint32_t src = MEM32(g_esp + 8);
    uint32_t n   = MEM32(g_esp + 12);

    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    g_eax = dst;
    g_esp += 4;                 /* ret: pop the return address */
}

/* sub_000F6BB0: the C runtime's memcpy (MSVC's, which also handles overlap),
 * cdecl (dst, src, n), returns dst. Its leftover bytes go through jump tables
 * (jmp [edx*4 + 0xF6CFC] and the like) into tails that end in `leave; ret`
 * on its own frame, which lift as separate functions with no frame of their
 * own: "Failed to resolve VA 0x000F6D34 / 0x000F6ED4" while the game copied
 * its cache files. Done natively. */
void sub_000F6BB0(void)
{
    uint32_t dst = MEM32(g_esp + 4);
    uint32_t src = MEM32(g_esp + 8);
    uint32_t n   = MEM32(g_esp + 12);

    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    g_eax = dst;
    g_esp += 4;                 /* ret: pop the return address */
}

/* sub_0009FC30: the engine's debug print, cdecl printf(fmt, ...) indented by
 * a nesting depth (0x206A5C). On the console it went to the debugger;
 * here it goes nowhere unless AR_DEBUG_PRINT=1, which sends it to stderr --
 * the engine says what it could not load or match ("No collision info on
 * object : %s", "Didn't find topology id:%d"), which is how a ported level
 * says what is wrong with it. */
static int ar_debug_print = -1;

static void ar_guest_format(char *out, size_t n, uint32_t fmt, uint32_t args)
{
    const char *f = (const char *)XBOX_PTR(fmt);
    size_t o = 0;
    while (*f && o + 1 < n) {
        char spec[16];
        size_t k = 0;
        if (*f != '%') {
            out[o++] = *f++;
            continue;
        }
        spec[k++] = *f++;
        while (*f && strchr("-+ #0123456789.l", *f) && k < sizeof spec - 2)
            spec[k++] = *f++;
        if (!*f)
            break;
        spec[k++] = *f;
        spec[k] = 0;
        switch (*f++) {
        case 's': {
            uint32_t p = MEM32(args);
            args += 4;
            o += snprintf(out + o, n - o, spec, p ? (const char *)XBOX_PTR(p) : "(null)");
            break;
        }
        case 'f': case 'g': case 'e': {
            double v;
            memcpy(&v, (const void *)XBOX_PTR(args), 8);
            args += 8;
            o += snprintf(out + o, n - o, spec, v);
            break;
        }
        case '%':
            out[o++] = '%';
            break;
        default:                        /* d i u x X c p */
            o += snprintf(out + o, n - o, spec, MEM32(args));
            args += 4;
            break;
        }
        if (o >= n)
            o = n - 1;
    }
    out[o] = 0;
}

void sub_0009FC30(void)
{
    if (ar_debug_print < 0) {
        const char *v = getenv("AR_DEBUG_PRINT");
        ar_debug_print = v && *v == '1';
    }
    if (ar_debug_print) {
        char msg[1024];
        ar_guest_format(msg, sizeof msg, MEM32(g_esp + 4), g_esp + 8);
        fprintf(stderr, "[AI] %s%s", msg,
                msg[0] && msg[strlen(msg) - 1] == '\n' ? "" : "\n");
    }
    g_esp += 4;                 /* ret: pop the return address */
}

/* Added levels. sub_00050050(name) turns a menu's level name ("Civic",
 * "Airfield", ... as the menu scripts set Root.FrontEnd.Level) into the
 * level's slot, 0-13 (14 for none); the slot picks its scene name and folder
 * from two tables of 32-byte names (0x1D9668, 0x1D99E8) and its progress
 * record. Levels a mod adds (levels.txt, tools/build_level_menu.py) have
 * Free Skate pages whose name is their scene; picked, they play in slot 9,
 * "LOCAL", which no menu offers, its two table entries pointed at the
 * level. They stay so -- the game asks again during play, and LOCAL has no
 * other use -- until something asks for Local itself. */
#define AR_SLOT_ADDED   9u
#define AR_SCENE_NAMES  0x1D9668u
#define AR_FOLDER_NAMES 0x1D99E8u

typedef struct { char scene[32], folder[32]; } ArAddedLevel;
static ArAddedLevel s_added[64];
static int s_added_count = -1;
static char s_local_scene[32], s_local_folder[32];

static void added_levels_load(void)
{
    const char *mods = getenv("RECOMP_MODS");
    s_added_count = 0;
    while (mods && *mods) {
        const char *end = strchr(mods, ';');
        size_t n = end ? (size_t)(end - mods) : strlen(mods);
        char path[512], line[256];
        FILE *f;
        if (n && n < sizeof path - 16) {
            memcpy(path, mods, n);
            strcpy(path + n, "\\levels.txt");
            if ((f = fopen(path, "r")) != NULL) {
                while (fgets(line, sizeof line, f) && s_added_count < 64) {
                    char name[96], scene[64], folder[64];
                    if (line[0] == '#' || sscanf(line, " %95[^|]| %63[^| \t\r\n] | %63[^| \t\r\n]",
                                                 name, scene, folder) != 3)
                        continue;
                    snprintf(s_added[s_added_count].scene, 32, "%s", scene);
                    snprintf(s_added[s_added_count].folder, 32, "%s", folder);
                    s_added_count++;
                }
                fclose(f);
                if (s_added_count) {
                    fprintf(stderr, "[AR] %d added levels (%s)\n", s_added_count, path);
                    break;              /* the first mod with a list: the menu's, all of them */
                }
            }
        }
        mods = end ? end + 1 : NULL;
    }
    memcpy(s_local_scene, (const void *)XBOX_PTR(AR_SCENE_NAMES + AR_SLOT_ADDED * 32), 32);
    memcpy(s_local_folder, (const void *)XBOX_PTR(AR_FOLDER_NAMES + AR_SLOT_ADDED * 32), 32);
}

static void slot_names(const char scene[32], const char folder[32])
{
    memcpy((void *)XBOX_PTR(AR_SCENE_NAMES + AR_SLOT_ADDED * 32), scene, 32);
    memcpy((void *)XBOX_PTR(AR_FOLDER_NAMES + AR_SLOT_ADDED * 32), folder, 32);
}

/* [scn] AR_SCENE_LOG=1: the scene each level start asks for (sub_000A07C0,
 * name in edx) and the level index it took it by (0x1E9FA4). Bring-up. */
extern void sub_000A07C0_gen(void);
void sub_000A07C0(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("AR_SCENE_LOG");
        on = v && *v == '1';
    }
    /* AR_TEST_SCENE=<scene>: every level load (the attract demo's too) loads
     * that scene instead -- a ported level tested without the menus. The
     * name is written into the level's own slot in the scene table. */
    {
        static const char *test = (const char *)-1;
        if (test == (const char *)-1)
            test = getenv("AR_TEST_SCENE");
        if (test && *test && g_edx) {
            char *slot = (char *)XBOX_PTR(g_edx);
            snprintf(slot, 32, "%s", test);
        }
    }
    if (on)
        fprintf(stderr, "[scn] load scene \"%s\" (level index %u, mode %u, loaded %08X, list %08X)\n",
                g_edx ? (const char *)XBOX_PTR(g_edx) : "(null)",
                (unsigned)MEM32(0x1E9FA4), (unsigned)MEM8(0x1E9FA3),
                (unsigned)MEM32(0x206C98), (unsigned)MEM32(0x206C94));
    sub_000A07C0_gen();
    if (on)
        fprintf(stderr, "[scn]   -> %u (loaded %08X)\n", (unsigned)g_eax, (unsigned)MEM32(0x206C98));
}

extern void sub_00050050_gen(void);
void sub_00050050(void)
{
    uint32_t arg = MEM32(g_esp + 4);
    const char *name = arg ? (const char *)XBOX_PTR(arg) : "";
    int i;

    if (s_added_count < 0)
        added_levels_load();
    for (i = 0; i < s_added_count; i++)
        if (!_stricmp(name, s_added[i].scene)) {
            static int shown = -1;
            if (shown != i) {
                fprintf(stderr, "[AR] added level %s: slot %u\n", s_added[i].scene, AR_SLOT_ADDED);
                shown = i;
            }
            slot_names(s_added[i].scene, s_added[i].folder);
            g_eax = AR_SLOT_ADDED;
            g_esp += 4;             /* ret: pop the return address */
            return;
        }
    sub_00050050_gen();
    if (g_eax == AR_SLOT_ADDED && s_added_count)
        slot_names(s_local_scene, s_local_folder);
}

/* sub_001003C2: D3DXMatrixPerspectiveFovLH(out, fovy, aspect, zn, zf),
 * stdcall. The game's cameras (sub_000C3D80, sub_000C3EE0) pass a fixed
 * aspect of 1.33 and the game never reads the console's 16:9 setting, so it
 * renders 4:3 only. With widescreen on ([display] widescreen, which also
 * shows the frame at 16:9), the 4:3 aspect becomes 16:9: fovy is vertical,
 * so the view widens at the sides (Hor+) and the 640x480 frame is drawn
 * anamorphic, which the window stretches back out. Other aspects pass
 * through. */
extern int xbox_FramebufferWindowWidescreen(void);
extern void sub_001003C2_gen(void);
void sub_001003C2(void)
{
    if (xbox_FramebufferWindowWidescreen()) {
        float aspect = MEMF(g_esp + 12);
        if (aspect > 1.32f && aspect < 1.34f)
            MEMF(g_esp + 12) = aspect * (16.0f / 9.0f) / (4.0f / 3.0f);
    }
    sub_001003C2_gen();
}

/* [coll] AR_COLL_PROBE=1: sub_000ACAE0, the collision query against one
 * object's collision tree (sphere, box, ray... by the shape at [esp+16]),
 * counted by shape and by hit (a nonzero return), every two seconds. A
 * ported level's floor that lets the skater through either is never
 * queried or is queried and missed; this says which. Bring-up probe. */
#include <time.h>
static int s_coll_on = -1;
static unsigned s_coll_obj_calls, s_coll_box_calls, s_coll_box_pass;
static unsigned s_coll_calls[5], s_coll_hits[5];

static void coll_report(void)
{
    static clock_t t0;
    if (clock() - t0 < 2 * CLOCKS_PER_SEC)
        return;
    t0 = clock();
    fprintf(stderr, "[coll] objects tried %u, box tests %u passed %u; queries by shape"
            " 0:%u/%u 1:%u/%u 2:%u/%u 3:%u/%u other:%u/%u (calls/hits)\n",
            s_coll_obj_calls, s_coll_box_calls, s_coll_box_pass,
            s_coll_calls[0], s_coll_hits[0], s_coll_calls[1], s_coll_hits[1],
            s_coll_calls[2], s_coll_hits[2], s_coll_calls[3], s_coll_hits[3],
            s_coll_calls[4], s_coll_hits[4]);
    s_coll_obj_calls = s_coll_box_calls = s_coll_box_pass = 0;
    memset(s_coll_calls, 0, sizeof s_coll_calls);
    memset(s_coll_hits, 0, sizeof s_coll_hits);
}

static int coll_on(void)
{
    if (s_coll_on < 0) {
        const char *v = getenv("AR_COLL_PROBE");
        s_coll_on = v && *v == '1';
    }
    return s_coll_on;
}

/* sub_000ACE20(object, query): one object against one query -- filters,
 * then a bounding-box test (sub_000AB270), then sub_000ACAE0. */
extern void sub_000ACE20_gen(void);
void sub_000ACE20(void)
{
    sub_000ACE20_gen();
    if (coll_on()) {
        s_coll_obj_calls++;
        coll_report();
    }
}

extern void sub_000AB270_gen(void);
void sub_000AB270(void)
{
    sub_000AB270_gen();
    if (coll_on()) {
        s_coll_box_calls++;
        if (g_eax)
            s_coll_box_pass++;
    }
}

extern void sub_000ACAE0_gen(void);
void sub_000ACAE0(void)
{
    uint32_t shape = MEM32(g_esp + 16);
    unsigned kind = shape ? MEM32(shape) : 4u;
    sub_000ACAE0_gen();
    if (!coll_on())
        return;
    if (kind > 4)
        kind = 4;
    s_coll_calls[kind]++;
    if (g_eax)
        s_coll_hits[kind]++;
    coll_report();
}

/* D3D's swap-slot waits, as spin-waits (lift.sh --spin-wait). Both are
 * `mov eax, [slot]; ... test eax, eax; jne <the test>`: the slot is read
 * once and the loop tests the register for ever. On the console the vblank
 * interrupt that frees the slot cannot land in the one instruction between
 * the read and the test often enough to matter; here the vblank runs on
 * another core, a read just before the flip saw the slot still busy, and
 * D3D's Reset (sub_00177540, called by the game when a loading screen
 * starts) waited on a slot that was already free: the game froze on a black
 * screen (2026-10-24). Each round reloads the register from the slot, as
 * the wait means to.
 *   0x00177573  sub_00177540: eax = [esi + ecx*4]
 *   0x0017F0A9  sub_0017F010: eax = [ecx + edx*4] */
static void ar_spin_wait_hook(uint32_t va)
{
    switch (va) {
    case 0x00177573u: g_eax = MEM32(g_esi + g_ecx * 4); break;
    case 0x0017F0A9u: g_eax = MEM32(g_ecx + g_edx * 4); break;
    }
}

void ar_manual_init(void)
{
    extern void (*g_xbox_spin_wait_hook)(uint32_t va);
    extern void xbox_Nv2aWidescreenHud(float z_marker);
    g_xbox_spin_wait_hook = ar_spin_wait_hook;
    {
        extern void ar_unlock_all_start(void);
        const char *u = getenv("AR_UNLOCK_ALL");
        if (u && *u == '1')
            ar_unlock_all_start();
    }
    /* The game's screen-space 2D (HUD, menus) carries z = 1.52596e6 on every
     * vertex, positions in 640x480 pixels: squeezed to its side when the
     * window is widescreen (the camera is widened in sub_001003C2). */
    xbox_Nv2aWidescreenHud(1.52596e6f);

    /* High frame rates. The frame function (sub_00089C70) times each frame
     * with QueryPerformanceCounter and clamps the step to 16.7..33.3 ms
     * (floats at 0x1A8200 and 0x1A81FC, used nowhere else): above 60 fps
     * every frame would still count 16.7 ms and the game would run fast.
     * With the vblank faster (RECOMP_VBLANK_HZ, the toolkit's guest vblank
     * rate; D3D presents once per vblank), the lower clamp becomes one
     * vblank at that rate, so the game steps by real time. */
    {
        const char *e = getenv("RECOMP_VBLANK_HZ");
        int hz = e ? atoi(e) : 60;
        if (hz > 60 && hz <= 360) {
            MEMF(0x1A8200) = 1000.0f / (float)hz;
            fprintf(stderr, "[AR] %d Hz: minimum frame step %.2f ms\n", hz,
                    (double)(1000.0f / (float)hz));
        }
    }
}

#include <windows.h>
/* [mp1] D3D's miniport swap state every 5 s, bring-up only: VBlankCount
 * (+0x1C0), SwapCount (+0x1BC), and per slot the flag (+0x174 + 12k) and
 * target vblank (+0x178 + 12k). Started from main.c's AR_MP1 hook. */
static DWORD WINAPI mp1_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        const uint32_t mp = 0x00189610u;
        Sleep(5000);
        fprintf(stderr, "[MP1] vblank %u swap %u | slot0 %u target %u | slot1 %u target %u\n",
                (unsigned)MEM32(mp + 0x1C0), (unsigned)MEM32(mp + 0x1BC),
                (unsigned)MEM32(mp + 0x174), (unsigned)MEM32(mp + 0x178),
                (unsigned)MEM32(mp + 0x180), (unsigned)MEM32(mp + 0x184));
        fflush(stderr);
    }
}
void ar_mp1_start(void)
{
    CreateThread(NULL, 0, mp1_thread, NULL, 0, NULL);
}

/* Everything unlocked ([game] unlock_all, AR_UNLOCK_ALL=1), from the first
 * frame of a fresh career. The game's own cheats (sub_00052520, typed on
 * its cheat screen) do it: "wwssadadBABA" sets each of the 21 level records
 * (0x510 bytes from 0x1EB209) to state 10 with every flag at +3 set, and
 * "sddsasasddd" sets 22 unlock bits at 0x1EA290. This does the same, once a
 * second, to any record below that -- so a new profile is unlocked as soon
 * as it exists and progress beyond it is never lowered -- without setting
 * the cheats-entered flags (0x1EA2A0) that mark a save as a cheat's. */
#define AR_LEVEL_RECORDS     0x1EB209u
#define AR_LEVEL_RECORD_SIZE 0x510u
#define AR_LEVEL_RECORD_END  0x1F1C59u
#define AR_UNLOCK_BITS       0x1EA290u

static DWORD WINAPI unlock_thread(LPVOID unused)
{
    unsigned applied = 0;
    (void)unused;
    for (;;) {
        uint32_t rec;
        int changed = 0;
        Sleep(1000);
        for (rec = AR_LEVEL_RECORDS; rec < AR_LEVEL_RECORD_END; rec += AR_LEVEL_RECORD_SIZE)
            if (MEM8(rec) < 0xA) {
                MEM8(rec) = 0xA;
                MEM32(rec + 3) = 0xFFFFFFFFu;
                changed = 1;
            }
        if ((MEM32(AR_UNLOCK_BITS) & 0x3FFFFFu) != 0x3FFFFFu) {
            MEM32(AR_UNLOCK_BITS) |= 0x3FFFFFu;
            changed = 1;
        }
        if (changed && applied++ < 8) {
            fprintf(stderr, "[AR] unlock_all: levels and unlocks set\n");
            fflush(stderr);
        }
    }
}

void ar_unlock_all_start(void)
{
    CreateThread(NULL, 0, unlock_thread, NULL, 0, NULL);
}
