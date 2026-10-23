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

/* [sw1] probe: D3D's swap (sub_0017F010) waits for (a) the GPU's DMA GET
 * (MEM32(device+0x23BC)+0x44) to reach device+0x24, then (b) for the swap
 * slot device+0x23C0+(0x1F+(device[0x257C]&1))*12 to be 0. A host thread
 * reports both every 5 s once the swap has been entered. Bring-up only. */
#include <windows.h>
extern void sub_0017F010_gen(void);
static volatile LONG s_sw_in, s_sw_calls;
extern volatile long g_ld_calls; extern unsigned long g_ld_tid;
volatile long g_sw_getwait, g_sw_slotwait, g_bf_enter, g_bf_spin, g_bf_kwait;
unsigned long g_sw_tid;
unsigned long GetCurrentThreadId_sw(void) { return GetCurrentThreadId(); }   /* [sw1] counted in the lifted loops */
static DWORD WINAPI sw_probe(LPVOID unused)
{
    (void)unused;
    for (;;) {
        uint32_t dev, regs, k;
        Sleep(5000);
        dev = MEM32(0x0018A2ACu);
        if (!dev) continue;
        regs = MEM32(dev + 0x23BC);
        k = MEM32(dev + 0x257C) & 1;
        fprintf(stderr, "[SW1] main=%lu loader calls=%ld tid=%lu bf enter=%ld spin=%ld kwait=%ld getwait=%ld slotwait=%ld in=%ld calls=%ld dev=%08X put(+24)=%08X regs=%08X user_put(+40)=%08X get(+44)=%08X"
                        " fence30=%08X p34=%08X *p34=%08X slot[%u]=%08X slot[other]=%08X 257C=%08X\n",
                g_sw_tid, g_ld_calls, g_ld_tid, g_bf_enter, g_bf_spin, g_bf_kwait, g_sw_getwait, g_sw_slotwait, s_sw_in, s_sw_calls, dev, MEM32(dev + 0x24), regs, regs ? MEM32(regs + 0x40) : 0,
                regs ? MEM32(regs + 0x44) : 0, MEM32(dev + 0x30), MEM32(dev + 0x34),
                MEM32(dev + 0x34) ? MEM32(MEM32(dev + 0x34)) : 0, k,
                MEM32(dev + 0x23C0 + (0x1F + k) * 12),
                MEM32(dev + 0x23C0 + (0x1F + (k ^ 1)) * 12), MEM32(dev + 0x257C));
        fflush(stderr);
    }
}
void sub_0017F010(void)
{
    static volatile LONG started;
    if (!InterlockedExchange(&started, 1))
        CreateThread(NULL, 0, sw_probe, NULL, 0, NULL);
    InterlockedIncrement(&s_sw_calls);
    g_sw_tid = GetCurrentThreadId();
    InterlockedIncrement(&s_sw_in);
    sub_0017F010_gen();
    InterlockedDecrement(&s_sw_in);
}

/* [sw1] who draws the loading screen while main spins: count by thread. */
extern void sub_000B4AC0_gen(void);
volatile long g_ld_calls; unsigned long g_ld_tid;
void sub_000B4AC0(void)
{
    g_ld_calls++;
    g_ld_tid = GetCurrentThreadId();
    sub_000B4AC0_gen();
}

/* [in1] which step of the game's Init (sub_00089BF0) fails. Bring-up only. */
extern void sub_000A06F0_gen(void);
void sub_000A06F0(void)
{
    fprintf(stderr, "[INIT] sub_000A06F0 ->\n");
    sub_000A06F0_gen();
    fprintf(stderr, "[INIT] sub_000A06F0 = %08X\n", g_eax);
}
extern void sub_0009FFD0_gen(void);
void sub_0009FFD0(void)
{
    fprintf(stderr, "[INIT] sub_0009FFD0 ->\n");
    sub_0009FFD0_gen();
    fprintf(stderr, "[INIT] sub_0009FFD0 = %08X\n", g_eax);
}
extern void sub_000A3500_gen(void);
void sub_000A3500(void)
{
    fprintf(stderr, "[INIT] sub_000A3500 -> name \"%.64s\"\n",
            g_eax ? (const char *)XBOX_PTR(g_eax) : "(null)");
    sub_000A3500_gen();
    fprintf(stderr, "[INIT] sub_000A3500 = %08X\n", g_eax);
}
extern void sub_000A31A0_gen(void);
void sub_000A31A0(void)
{
    fprintf(stderr, "[INIT] sub_000A31A0 ->\n");
    sub_000A31A0_gen();
    fprintf(stderr, "[INIT] sub_000A31A0 = %08X\n", g_eax);
    {
        int k;
        fprintf(stderr, "[INIT]   recent ICALL targets:");
        for (k = 0; k < 16; k++)
            fprintf(stderr, " %08X", g_icall_trace[(g_icall_trace_idx + k) & 15]);
        fprintf(stderr, "\n");
    }
}
extern void sub_000A2AA0_gen(void);
void sub_000A2AA0(void)
{
    fprintf(stderr, "[INIT] sub_000A2AA0 ->\n");
    sub_000A2AA0_gen();
    fprintf(stderr, "[INIT] sub_000A2AA0 = %08X\n", g_eax);
}

/* [sc1] the scene path being resolved (sub_000A96D0(path, Node **out)). */
extern void sub_000A96D0_gen(void);
void sub_000A96D0(void)
{
    static char last[96];
    static unsigned repeats;
    uint32_t path = MEM32(g_esp + 4);
    if (path) {
        const char *p = (const char *)XBOX_PTR(path);
        if (strncmp(p, last, sizeof last - 1)) {
            if (repeats)
                fprintf(stderr, "[SCENE]   (x%u)\n", repeats + 1);
            fprintf(stderr, "[SCENE] resolve \"%.80s\"\n", p);
            strncpy(last, p, sizeof last - 1);
            repeats = 0;
        } else
            repeats++;
    }
    sub_000A96D0_gen();
}
