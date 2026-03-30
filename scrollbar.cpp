/*
 * scrollbar.cpp — Reusable RichEdit scrollbar manager.
 *
 * Works for BOTH writable and read-only (EM_SETREADONLY) RichEdit controls.
 *
 * Root cause of all previous failures:
 *   A writable, focused RichEdit updates SCROLLINFO.nPos itself after processing
 *   WM_VSCROLL — the thumb moves automatically.  A read-only or unfocused
 *   RichEdit scrolls the content correctly but never writes back nPos, so any
 *   post-scroll read (GetScrollInfo, EM_GETFIRSTVISIBLELINE, EM_GETSCROLLPOS)
 *   returns stale or wrong-unit data.
 *
 * Solution — pre-scroll delta arithmetic:
 *   Read GetScrollInfo(SIF_ALL) BEFORE CallWindowProcW.  The pre-scroll nPos
 *   is always correct and fresh.  Compute the new position by applying the
 *   scroll code's delta in the scrollbar's own coordinate space (line units).
 *   Let the RichEdit scroll natively via CallWindowProcW, then call
 *   SetScrollPos to our pre-computed value.  No post-scroll read needed.
 *
 *   For mousewheel: same — read nPos before EM_SCROLL, add/subtract N lines,
 *   clamp, then SetScrollPos.
 *
 *   This is coordinate-space safe: nPos, nMax, nPage, nTrackPos are all in
 *   the same line-index units.  No pixel arithmetic.
 */

#include "scrollbar.h"
#include <richedit.h>   /* EM_SCROLL */
#include <stdlib.h>     /* malloc / free */

/* ── Internal context ───────────────────────────────────────────────────────*/

struct RichEditScrollbarCtx {
    HWND    hRE;        /* the managed RichEdit window                        */
    WNDPROC origProc;   /* saved original window proc, restored on Detach     */
};

/* Window property name used to retrieve the context from inside the subclass. */
static const wchar_t* kRescProp = L"RESC_Ctx";

/* Forward declaration of the subclass proc. */
static LRESULT CALLBACK RESC_SubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam);

/* ── Helper: compute new scrollbar position from a WM_VSCROLL code ─────────
 * Reads GetScrollInfo before any scroll so the current position is always
 * fresh.  Returns the new clamped position.
 * Also fills *pSi with the SCROLLINFO for the caller's use.
 */
static int RESC_ComputeNewPos(HWND hwnd, WPARAM wParam, SCROLLINFO* pSi)
{
    pSi->cbSize = sizeof(*pSi);
    pSi->fMask  = SIF_ALL;
    GetScrollInfo(hwnd, SB_VERT, pSi);

    int maxPos = pSi->nMax - (int)pSi->nPage + 1;
    if (maxPos < 0) maxPos = 0;

    int newPos = pSi->nPos;
    switch (LOWORD(wParam)) {
        case SB_LINEUP:        newPos--;                        break;
        case SB_LINEDOWN:      newPos++;                        break;
        case SB_PAGEUP:        newPos -= (int)pSi->nPage;       break;
        case SB_PAGEDOWN:      newPos += (int)pSi->nPage;       break;
        case SB_TOP:           newPos  = pSi->nMin;             break;
        case SB_BOTTOM:        newPos  = maxPos;                break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: newPos  = (int)pSi->nTrackPos;   break;
        default:               newPos  = pSi->nPos;             break;
    }
    if (newPos < pSi->nMin) newPos = pSi->nMin;
    if (newPos > maxPos)    newPos = maxPos;
    return newPos;
}

/* ── Public API ─────────────────────────────────────────────────────────────*/

HRESC RESC_Attach(HWND hRichEdit)
{
    if (!hRichEdit || !IsWindow(hRichEdit)) return NULL;

    RichEditScrollbarCtx* ctx =
        (RichEditScrollbarCtx*)malloc(sizeof(RichEditScrollbarCtx));
    if (!ctx) return NULL;

    ctx->hRE      = hRichEdit;
    ctx->origProc = (WNDPROC)(LONG_PTR)
        SetWindowLongPtrW(hRichEdit, GWLP_WNDPROC, (LONG_PTR)RESC_SubclassProc);

    if (!ctx->origProc) {
        free(ctx);
        return NULL;
    }

    /* Store the context on the window so the subclass proc can retrieve it. */
    SetPropW(hRichEdit, kRescProp, (HANDLE)ctx);
    return (HRESC)ctx;
}

void RESC_Detach(HRESC h)
{
    if (!h) return;
    RichEditScrollbarCtx* ctx = (RichEditScrollbarCtx*)h;
    if (ctx->hRE && IsWindow(ctx->hRE)) {
        SetWindowLongPtrW(ctx->hRE, GWLP_WNDPROC, (LONG_PTR)ctx->origProc);
        RemovePropW(ctx->hRE, kRescProp);
    }
    free(ctx);
}

void RESC_SyncThumb(HRESC h)
{
    /* Force the thumb to reflect the current scroll position.
     * Use after EM_SETSCROLLPOS or a programmatic scroll that bypasses
     * WM_VSCROLL so the subclass proc is not invoked. */
    if (!h) return;
    RichEditScrollbarCtx* ctx = (RichEditScrollbarCtx*)h;
    if (!ctx->hRE || !IsWindow(ctx->hRE)) return;

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(ctx->hRE, SB_VERT, &si);
    SetScrollPos(ctx->hRE, SB_VERT, si.nPos, TRUE);
}

LRESULT RESC_OnMouseWheel(HRESC h, WPARAM wParam)
{
    /* Scroll the RichEdit content, then sync the thumb using the pre-scroll
     * delta approach: compute newPos from nPos BEFORE EM_SCROLL so we never
     * rely on the RichEdit updating nPos synchronously. */
    if (!h) return 0;
    RichEditScrollbarCtx* ctx = (RichEditScrollbarCtx*)h;
    if (!ctx->hRE || !IsWindow(ctx->hRE)) return 0;

    /* Read state before scrolling. */
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(ctx->hRE, SB_VERT, &si);
    int maxPos = si.nMax - (int)si.nPage + 1;
    if (maxPos < 0) maxPos = 0;

    int  delta = GET_WHEEL_DELTA_WPARAM(wParam);
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == WHEEL_PAGESCROLL) lines = 3;
    int count = (abs(delta) * (int)lines + WHEEL_DELTA / 2) / WHEEL_DELTA;

    int newPos = si.nPos + (delta > 0 ? -count : count);
    if (newPos < si.nMin) newPos = si.nMin;
    if (newPos > maxPos)  newPos = maxPos;

    WPARAM cmd = (delta > 0) ? (WPARAM)SB_LINEUP : (WPARAM)SB_LINEDOWN;
    for (int i = 0; i < count; ++i)
        SendMessageW(ctx->hRE, EM_SCROLL, cmd, 0);

    SetScrollPos(ctx->hRE, SB_VERT, newPos, TRUE);
    return 0;
}

/* ── Subclass proc ──────────────────────────────────────────────────────────*/

static LRESULT CALLBACK RESC_SubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam)
{
    RichEditScrollbarCtx* ctx =
        (RichEditScrollbarCtx*)GetPropW(hwnd, kRescProp);
    if (!ctx)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    if (msg == WM_VSCROLL) {
        if (LOWORD(wParam) == SB_ENDSCROLL)
            return CallWindowProcW(ctx->origProc, hwnd, msg, wParam, lParam);

        /* Pre-scroll delta: compute desired thumb position NOW from the fresh
         * pre-scroll state, let the RichEdit scroll natively, then set the
         * thumb to the pre-computed position.  Works for both writable and
         * read-only RichEdit controls. */
        SCROLLINFO si = {};
        int newPos = RESC_ComputeNewPos(hwnd, wParam, &si);

        LRESULT r = CallWindowProcW(ctx->origProc, hwnd, msg, wParam, lParam);
        SetScrollPos(hwnd, SB_VERT, newPos, TRUE);
        return r;
    }

    return CallWindowProcW(ctx->origProc, hwnd, msg, wParam, lParam);
}