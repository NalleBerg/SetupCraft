/*
 * my_scrollbar.cpp — Custom Win32 scrollbar implementation.
 *
 * PHASE 1: Custom window class, static drawing, DPI-aware layout.
 *          No mouse interaction yet — that is Phase 2.
 *
 * Architecture overview:
 *   Each msb_attach() call allocates a MsbCtx, creates a child window
 *   (class "MyScrollbar") positioned on the right or bottom edge of the
 *   target window, and subclasses the target window to intercept WM_SIZE
 *   (so the bar repositions on resize).  The bar window has its own
 *   WM_PAINT that reads SCROLLINFO from the target and draws everything.
 *
 *   The native scrollbar is hidden (ShowScrollBar FALSE) but the WS_VSCROLL
 *   flag is kept so the target window continues to maintain SCROLLINFO
 *   internally.  We only read SCROLLINFO — we never let the native bar draw.
 *
 * DPI handling:
 *   All baseline constants in my_scrollbar.h are in 96-DPI pixels.
 *   At creation time we read the DPI of the target window and compute a
 *   scale factor.  S(n) scales a baseline value to the actual DPI.
 *
 * Drawing:
 *   Double-buffered (off-screen DC + BitBlt) to prevent flicker.
 *   Rounded rectangles drawn with RoundRect().
 *   Arrow glyphs drawn as filled polygons (three-point triangle).
 *   All hit-test rectangles (rArrowUp, rArrowDown, rThumb, rTrack) are
 *   stored in MsbCtx and recomputed in Msb_Layout() on every WM_SIZE.
 *
 * Color/state:
 *   Phase 1 draws everything in the "normal" state.
 *   Hover/drag state enums are declared here ready for Phase 2/3.
 */

#include "my_scrollbar_vscroll.h"
#include <stdlib.h>     /* malloc / free */
#include <windowsx.h>   /* GET_X_LPARAM / GET_Y_LPARAM */
#include <commctrl.h>   /* LVM_SCROLL, ListView_GetItemRect, etc. */
#include <algorithm>    /* std::min / std::max */
using std::min;
using std::max;

/* ── Internal enums ─────────────────────────────────────────────────────────*/

typedef enum {
    THUMB_NORMAL = 0,
    THUMB_HOVER,
    THUMB_DRAG
} ThumbState;

typedef enum {
    ARROW_NORMAL = 0,
    ARROW_HOVER,
    ARROW_PRESSED
} ArrowState;

typedef enum {
    FADE_INVISIBLE = -1, /* window hidden entirely — content fits viewport   */
    FADE_HIDDEN = 0,     /* thin strip (MSB_WIDTH_HIDDEN); not interactable  */
    FADE_EXPANDING,      /* animating toward MSB_WIDTH_FULL                  */
    FADE_VISIBLE,        /* at MSB_WIDTH_FULL; fully interactive             */
    FADE_CONTRACTING     /* animating back toward MSB_WIDTH_HIDDEN           */
} FadeState;

/* ── Internal context ───────────────────────────────────────────────────────*/

typedef struct MsbCtx {
    /* Ownership */
    HWND    hBar;           /* the custom scrollbar child window              */
    HWND    hTarget;        /* the window being scrolled                      */
    WNDPROC origProc;       /* saved target window proc (restored on detach)  */
    DWORD   flags;          /* MSB_* flags passed to msb_attach               */
    BOOL    isRichEdit;     /* TRUE if hTarget is a RichEdit                  */
    BOOL    isListView;     /* TRUE if hTarget is a SysListView32             */
    BOOL    isTreeView;     /* TRUE if hTarget is a SysTreeView32             */
    BOOL    inWmSize;       /* re-entrancy guard: TRUE while WM_SIZE is being processed */
    BOOL    inNcPaint;      /* re-entrancy guard: TRUE while WM_NCPAINT is being processed */

    /* DPI */
    float   dpiScale;       /* actual DPI / 96.0f                             */

    /* Computed layout (recalculated by Msb_Layout on every WM_SIZE) */
    RECT    rArrowUp;       /* hit rect for top/left arrow                    */
    RECT    rArrowDown;     /* hit rect for bottom/right arrow                */
    RECT    rTrack;         /* track area between arrows                      */
    RECT    rThumb;         /* current thumb rect, within rTrack              */

    /* Interaction state (used from Phase 2 onward) */
    ThumbState thumbState;
    ArrowState arrowUpState;
    ArrowState arrowDownState;
    BOOL    dragging;
    int     dragStartPx;    /* cursor position when drag started              */
    int     dragStartPos;   /* nPos when drag started                         */
    int     dragCurPos;     /* current dragged nPos (used on WM_LBUTTONUP)    */

    /* Auto-repeat (Phase 2) */
    UINT_PTR timerId;       /* 0 when no timer is active                      */
    UINT    repeatCmd;      /* SB_LINEUP / SB_LINEDOWN / SB_PAGEUP / PAGEDOWN */
    BOOL    timerFirst;     /* TRUE on first tick → use long initial delay    */

    /* Mousewheel sub-tick accumulator (Phase 2) */
    int     wheelAccum;     /* accumulated wheel delta × sysLines, mod WHEEL_DELTA */

    /* Hover tracking (Phase 3) */
    BOOL    trackingMouse;  /* TRUE while TrackMouseEvent(TME_LEAVE) is active  */

    /* Fade / hidden-mode animation (Phase 4) */
    FadeState fadeState;    /* current animation state                         */
    float     fadeWidth;    /* current bar thickness in scaled px (fractional) */

    /* Horizontal RichEdit content width (pixels).
     * Measured by Msb_MeasureRichEditContentW() and cached here.
     * 0 = not yet measured / use SCROLLINFO fallback. */
    int     richHorzMax;

    /* Vertical RichEdit document height (pixels).
     * EM_SHOWSCROLLBAR(FALSE) stops RichEdit from maintaining GetScrollInfo, so
     * we measure document height directly from character positions and cache it.
     * 0 = not yet measured. */
    int     richVertMax;

    /* Optional insets (px, scaled) for the bar window along its axis.
     * For a vertical bar: nearEdge = top, farEdge = bottom of the target's
     * client area to exclude (e.g. toolbar height, status bar height).
     * Set via msb_set_insets(); 0 by default. */
    int     insetNear;
    int     insetFar;

    /* Optional gap (px, unscaled) between the bar and the window edge.
     * Shifts a vertical bar left / horizontal bar up so it sits slightly
     * inside the edge rather than flush against it. Set via msb_set_edge_gap(). */
    int     edgeGap;

    /* Tracked actual horizontal scroll position for ListView H-axis (pixels).
     * GetScrollInfo(SB_HORZ).nPos is unreliable: ShowScrollBar(FALSE) zeroes
     * it, and LVM_SCROLL does not update it.  We maintain the authoritative
     * value here and use it exclusively instead of GetScrollInfo for nPos.
     * Initialised to 0 (calloc); updated by every H scroll operation. */
    int     lvHPos;

    /* Set TRUE by MsbH_DeliverScroll while an LVM_SCROLL is in flight.
     * The WM_NCPAINT intercept skips Msb_HideNativeBar for H while this flag
     * is set — preventing ShowScrollBar(FALSE) from zeroing the ListView's
     * internal scroll counter mid-delivery, which would block leftward scroll. */
    BOOL    inHDeliver;
} MsbCtx;

/* Window property key to retrieve MsbCtx from the bar window proc. */
static const wchar_t* kMsbBarProp    = L"MSB_Bar";

/* Window property keys to retrieve MsbCtx from the TARGET window proc.
 * Separate keys for vertical and horizontal so two bars can share a target. */
static const wchar_t* kMsbTargetPropV = L"MSB_Target_V";
static const wchar_t* kMsbTargetPropH = L"MSB_Target_H";

static const wchar_t* Msb_TargetPropKey(DWORD flags)
{
    return (flags & MSB_HORIZONTAL) ? kMsbTargetPropH : kMsbTargetPropV;
}
static const wchar_t* Msb_OtherTargetPropKey(DWORD flags)
{
    return (flags & MSB_HORIZONTAL) ? kMsbTargetPropV : kMsbTargetPropH;
}

/* Forward declarations */
static LRESULT CALLBACK Msb_BarWndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK Msb_TargetSubclassProc(HWND hwnd, UINT msg,
                                                WPARAM wParam, LPARAM lParam);
static void Msb_Layout(MsbCtx* ctx);
static void Msb_PositionBar(MsbCtx* ctx);

/* ── DPI helper ─────────────────────────────────────────────────────────────*/

/* Scale a 96-DPI baseline value to the context's actual DPI. */
static inline int S(const MsbCtx* ctx, int px)
{
    return (int)(px * ctx->dpiScale + 0.5f);
}

static float Msb_GetDpiScale(HWND hWnd)
{
    /* GetDpiForWindow requires Windows 10 1607+. Fall back to system DPI. */
    UINT dpi = 96;
#if defined(GetDpiForWindow)
    dpi = GetDpiForWindow(hWnd);
    if (dpi == 0) dpi = 96;
#else
    HDC hdc = GetDC(hWnd);
    if (hdc) {
        dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(hWnd, hdc);
    }
#endif
    return (float)dpi / 96.0f;
}

/* ── Window class registration ──────────────────────────────────────────────*/

static BOOL s_classRegistered = FALSE;

static BOOL Msb_EnsureClass(HINSTANCE hInst)
{
    if (s_classRegistered) return TRUE;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = Msb_BarWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;  /* we paint everything ourselves */
    wc.lpszClassName = L"MyScrollbar";

    if (!RegisterClassExW(&wc)) return FALSE;
    s_classRegistered = TRUE;
    return TRUE;
}

/* EM_SHOWSCROLLBAR — defined in richedit.h but we avoid that header. */
#ifndef EM_SHOWSCROLLBAR
#define EM_SHOWSCROLLBAR 0x0460
#endif

/* EM_GETSCROLLPOS / EM_SETSCROLLPOS — pixel-based scroll for RichEdit.
 * Always reflects / sets the actual position even when native bar is hidden. */
#ifndef EM_GETSCROLLPOS
#define EM_GETSCROLLPOS  (WM_USER + 221)
#endif
#ifndef EM_SETSCROLLPOS
#define EM_SETSCROLLPOS  (WM_USER + 222)
#endif

/* ── Native scrollbar suppression / restoration ─────────────────────────────*/

/* After ShowScrollBar(FALSE) for a TreeView V-axis, the control resets
 * SB_VERT SCROLLINFO entirely (nMin=nMax=nPage=nPos=0).  This makes the
 * TreeView think scroll position is 0, so SB_LINEUP/SB_LINEDOWN in
 * WM_VSCROLL do nothing and Msb_Layout draws a full-height thumb.
 * Fix: reconstruct SCROLLINFO from live tree state after every HideNativeBar
 * on the V-axis for a TreeView.  Walk TVGN_NEXTVISIBLE to count total
 * visible rows and the index of the first-visible item (scroll position). */
static void Msb_ReconstructTreeVScrollInfo(HWND hWnd)
{
    int itemH = (int)SendMessageW(hWnd, TVM_GETITEMHEIGHT, 0, 0);
    if (itemH <= 0) return;
    RECT rc; GetClientRect(hWnd, &rc);
    int nPage = rc.bottom / itemH;
    if (nPage < 1) nPage = 1;

    HTREEITEM hFirstVis = (HTREEITEM)SendMessageW(hWnd, TVM_GETNEXTITEM,
                                                   TVGN_FIRSTVISIBLE, 0);
    HTREEITEM hItem = (HTREEITEM)SendMessageW(hWnd, TVM_GETNEXTITEM,
                                               TVGN_ROOT, 0);
    int nTotal = 0, nPos = 0;
    BOOL foundFirst = (hFirstVis == NULL);  /* treat NULL first-vis as pos=0 */
    while (hItem) {
        if (!foundFirst) {
            if (hItem == hFirstVis) foundFirst = TRUE;
            else nPos++;
        }
        nTotal++;
        hItem = (HTREEITEM)SendMessageW(hWnd, TVM_GETNEXTITEM,
                                         TVGN_NEXTVISIBLE, (LPARAM)hItem);
    }

    if (nTotal == 0) return;
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = nTotal - 1;
    si.nPage  = (UINT)nPage;
    si.nPos   = nPos;
    SetScrollInfo(hWnd, SB_VERT, &si, FALSE);
}

static void Msb_HideNativeBar(HWND hWnd, BOOL isRichEdit, BOOL vert)
{
    int which = vert ? SB_VERT : SB_HORZ;
    if (isRichEdit)
        SendMessageW(hWnd, EM_SHOWSCROLLBAR, (WPARAM)which, FALSE);
    else
        ShowScrollBar(hWnd, which, FALSE);
}

static void Msb_ShowNativeBar(HWND hWnd, BOOL isRichEdit, BOOL vert)
{
    int which = vert ? SB_VERT : SB_HORZ;
    if (isRichEdit)
        SendMessageW(hWnd, EM_SHOWSCROLLBAR, (WPARAM)which, TRUE);
    else
        ShowScrollBar(hWnd, which, TRUE);
}

/* ── RichEdit detection ─────────────────────────────────────────────────────*/

static BOOL Msb_IsRichEdit(HWND hWnd)
{
    wchar_t cls[64] = {};
    GetClassNameW(hWnd, cls, 64);
    return (lstrcmpiW(cls, L"RICHEDIT50W")  == 0 ||
            lstrcmpiW(cls, L"RichEdit20W")  == 0 ||
            lstrcmpiW(cls, L"RichEdit20A")  == 0);
}

static BOOL Msb_IsListView(HWND hWnd)
{
    wchar_t cls[64] = {};
    GetClassNameW(hWnd, cls, 64);
    return (lstrcmpiW(cls, L"SysListView32") == 0);
}

static BOOL Msb_IsTreeView(HWND hWnd)
{
    wchar_t cls[64] = {};
    GetClassNameW(hWnd, cls, 64);
    return (lstrcmpiW(cls, L"SysTreeView32") == 0);
}

/* ── Horizontal RichEdit content-width measurement ─────────────────────────*/

/*
 * Measures the actual rendered pixel width of the widest line in a no-wrap
 * RichEdit by iterating lines and asking EM_POSFROMCHAR for the last char
 * position on each line.  Returns the widest x-coordinate found, or 0.
 *
 * This is needed because EM_SETTARGETDEVICE fixes nMax to the target device
 * width (e.g. 32767 px) regardless of actual content width, making the
 * SCROLLINFO-based thumb hopelessly tiny for short documents.
 */
/* Measures the horizontal content extent of a RichEdit window by scanning
 * every line's character positions.  Returns the rightmost x-coordinate
 * (document space), or 0 on failure. */
static int Msb_MeasureRichHorzMax(HWND hRE)
{
    /* EM_POSFROMCHAR returns CLIENT coordinates — add the current horizontal
     * scroll offset to convert to document (content) coordinates. */
    POINT scrollPt = {0, 0};
    SendMessageW(hRE, EM_GETSCROLLPOS, 0, (LPARAM)&scrollPt);
    int horzOff = scrollPt.x;

    int lineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
    if (lineCount <= 0) return 0;
    int maxX = 0;
    for (int ln = 0; ln < lineCount; ln++) {
        int firstChar = (int)SendMessageW(hRE, EM_LINEINDEX, (WPARAM)ln, 0);
        int lineLen   = (int)SendMessageW(hRE, EM_LINELENGTH, (WPARAM)firstChar, 0);
        /* Use the position AFTER the last char on this line — that is the right
         * edge of the last rendered character.  EM_LINELENGTH excludes CR/LF,
         * so firstChar + lineLen is either the newline char or one past the
         * document end; both give the right edge we need.  Empty lines (lineLen
         * == 0) fall back to firstChar, which is fine. */
        int afterLastChar = firstChar + lineLen;
        POINTL pt = {};
        SendMessageW(hRE, EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM)afterLastChar);
        /* Convert client-x to document-x by adding horizontal scroll offset. */
        int docX = (int)pt.x + horzOff;
        if (docX > maxX) maxX = docX;
    }
    return maxX;
}

/* Measures the total document height of a RichEdit in pixels.
 * EM_SHOWSCROLLBAR(SB_VERT, FALSE) stops RichEdit updating GetScrollInfo, so
 * we compute height from EM_POSFROMCHAR + EM_GETSCROLLPOS instead.
 * The result is the pixel coordinate of the bottom of the last line. */
static int Msb_MeasureRichVertMax(HWND hRE)
{
    POINT scrollPt = {0, 0};
    SendMessageW(hRE, EM_GETSCROLLPOS, 0, (LPARAM)&scrollPt);
    int vertOff = scrollPt.y;

    int lineCount = (int)SendMessageW(hRE, EM_GETLINECOUNT, 0, 0);
    if (lineCount <= 0) return 1;

    int lastLine = lineCount - 1;
    int lastChar = (int)SendMessageW(hRE, EM_LINEINDEX, lastLine, 0);
    POINTL ptLast = {};
    SendMessageW(hRE, EM_POSFROMCHAR, (WPARAM)&ptLast, (LPARAM)lastChar);

    /* Measure the last line's actual height by comparing it with the line
     * immediately above it.  Using lines 0-1 would give the wrong height for
     * mixed-font documents (e.g. a large title followed by small italic text)
     * because the first-line spacing is unrepresentative of the final line. */
    int lineH = 0;
    if (lastLine > 0) {
        int prevChar = (int)SendMessageW(hRE, EM_LINEINDEX, lastLine - 1, 0);
        POINTL ptPrev = {};
        SendMessageW(hRE, EM_POSFROMCHAR, (WPARAM)&ptPrev, (LPARAM)prevChar);
        lineH = (int)(ptLast.y - ptPrev.y);
    }
    if (lineH <= 0) {
        /* Single-line doc or measurement failed — fall back to text metrics. */
        HDC hdc = GetDC(hRE);
        if (hdc) {
            TEXTMETRICW tm = {};
            GetTextMetricsW(hdc, &tm);
            ReleaseDC(hRE, hdc);
            lineH = tm.tmHeight + tm.tmExternalLeading;
        }
    }
    if (lineH <= 0) lineH = 16; /* ultimate fallback */

    /* Bottom of last line = top-of-last-line + line-height + scroll offset. */
    int docBottom = (int)ptLast.y + vertOff + lineH;
    return max(1, docBottom);
}

/* ── Overflow helper ────────────────────────────────────────────────────────*/

/*
 * Clamp the horizontal scroll position of a RichEdit to the measured content
 * width.  Called after every WM_HSCROLL / WM_MOUSEHWHEEL so the user cannot
 * scroll into the dead space that EM_SETTARGETDEVICE adds beyond real content.
 */
static void Msb_ClampRichHorzPos(MsbCtx* ctx)
{
    if (!ctx || !ctx->isRichEdit || !(ctx->flags & MSB_HORIZONTAL)) return;
    if (ctx->richHorzMax <= 0) return;   /* not yet measured */
    RECT rcT; GetClientRect(ctx->hTarget, &rcT);
    int margin = S(ctx, 24);
    int maxPos = ctx->richHorzMax + margin - rcT.right;
    if (maxPos < 0) maxPos = 0;
    POINT pt = {0, 0};
    SendMessageW(ctx->hTarget, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
    if (pt.x > maxPos) {
        pt.x = maxPos;
        SendMessageW(ctx->hTarget, EM_SETSCROLLPOS, 0, (LPARAM)&pt);
    }
}

/* Measure total column width of a ListView (sum of all ListView_GetColumnWidth). */
static int Msb_ListViewTotalColumnWidth(HWND hLV)
{
    HWND hHdr = ListView_GetHeader(hLV);
    if (!hHdr) return 0;
    int n = Header_GetItemCount(hHdr);
    int total = 0;
    for (int i = 0; i < n; i++)
        total += ListView_GetColumnWidth(hLV, i);
    return total;
}

/* Read the true H scroll position from the header control.
 * In report mode, the header items scroll with the content: when the
 * ListView is scrolled right by X pixels, column 0's left edge in
 * header-client coordinates is at -X.  This is always accurate —
 * GetScrollInfo.nPos is unreliable because ShowScrollBar(FALSE) zeroes it
 * and WM_NCPAINT may fire before ListView commits its internal nPos. */
static int Msb_GetListViewHPos(HWND hLV)
{
    HWND hHdr = ListView_GetHeader(hLV);
    if (!hHdr || Header_GetItemCount(hHdr) == 0) return 0;
    RECT rc = {};
    if (!Header_GetItemRect(hHdr, 0, &rc)) return 0;
    return max(0, -(int)rc.left);
}

/* Populate a SCROLLINFO with correct values for ListView H-axis.
 * GetScrollInfo(SB_HORZ) nMax and nPage are unreliable for ListView in
 * report mode: ShowScrollBar(FALSE) zeroes them, and the ListView may not
 * maintain pixel-accurate values externally.  We compute:
 *   nMax  = total column width - 1  (gives scroll range = totalW - clientW)
 *   nPage = client area width in pixels
 *   nPos  = GetScrollInfo().nPos    (reliable: stays in pixels)
 * si must already have cbSize set before calling. */
static void Msb_FixListViewHScrollInfo(HWND hLV, SCROLLINFO* si)
{
    int totalW  = Msb_ListViewTotalColumnWidth(hLV);
    RECT rcC; GetClientRect(hLV, &rcC);
    int clientW = rcC.right;

    /* Preserve nPos from the live SCROLLINFO. */
    SCROLLINFO live = {sizeof(live), SIF_POS};
    GetScrollInfo(hLV, SB_HORZ, &live);

    si->nMin  = 0;
    si->nMax  = (totalW > 0) ? (totalW - 1) : 0;
    si->nPage = (UINT)max(0, clientW);
    si->nPos  = live.nPos;
    /* Clamp nPos to valid scroll range. */
    int maxPos = max(0, (int)(si->nMax - (int)si->nPage + 1));
    if (si->nPos < 0)      si->nPos = 0;
    if (si->nPos > maxPos) si->nPos = maxPos;
}

/*
 * Returns TRUE when there is content beyond the visible viewport on this
 * axis — i.e. when a scrollbar is actually needed.  For auto-hide bars the
 * bar window is fully hidden (not even the thin strip) when this is FALSE.
 */
static BOOL Msb_ContentOverflows(MsbCtx* ctx)
{
    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
    /* Horizontal RichEdit: use the measured content width, not SCROLLINFO
     * nMax (which reflects the fixed EM_SETTARGETDEVICE line width). */
    if (!vert && ctx->isRichEdit) {
        if (ctx->richHorzMax <= 0)
            ctx->richHorzMax = Msb_MeasureRichHorzMax(ctx->hTarget);
        RECT rcT; GetClientRect(ctx->hTarget, &rcT);
        return (ctx->richHorzMax + S(ctx, 24) > rcT.right);
    }
    /* Vertical RichEdit: SCROLLINFO nMax is stale when the native bar is hidden;
     * compare the directly measured document height against the viewport. */
    if (vert && ctx->isRichEdit) {
        if (ctx->richVertMax <= 0)
            ctx->richVertMax = Msb_MeasureRichVertMax(ctx->hTarget);
        if (ctx->richVertMax > 0) {
            RECT rcT; GetClientRect(ctx->hTarget, &rcT);
            return (ctx->richVertMax > rcT.bottom);
        }
    }
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(ctx->hTarget, vert ? SB_VERT : SB_HORZ, &si);
    if (ctx->isRichEdit) {
        RECT rcT; GetClientRect(ctx->hTarget, &rcT);
        int clientLen = vert ? rcT.bottom : rcT.right;
        if (clientLen > 0) si.nPage = (UINT)clientLen;
        POINT pt = {0, 0};
        SendMessageW(ctx->hTarget, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
        si.nPos = vert ? pt.y : pt.x;
    }
    /* TreeView V-axis: SCROLLINFO nMax/nPage are reset to 0 by ShowScrollBar(FALSE)
     * and are NOT reliably repopulated by origProc(WM_SIZE) when the client size
     * hasn't changed (TreeView caches its size).  Bypass SCROLLINFO entirely:
     * walk the tree in expanded/visible order (TVGN_NEXTVISIBLE from root) and
     * compare the expanded-row count against client-height / item-height capacity.
     * Early exit keeps this O(overflow_point), which is fast in practice. */
    if (ctx->isTreeView && vert && si.nPage == 0) {
        RECT rcTv; GetClientRect(ctx->hTarget, &rcTv);
        int itemH = (int)SendMessageW(ctx->hTarget, TVM_GETITEMHEIGHT, 0, 0);
        if (itemH <= 0) return FALSE;
        int capacity = rcTv.bottom / itemH;
        int nRows = 0;
        HTREEITEM hItem = (HTREEITEM)SendMessageW(ctx->hTarget,
                                                   TVM_GETNEXTITEM, TVGN_ROOT, 0);
        while (hItem) {
            if (++nRows > capacity) return TRUE;
            hItem = (HTREEITEM)SendMessageW(ctx->hTarget,
                                             TVM_GETNEXTITEM, TVGN_NEXTVISIBLE,
                                             (LPARAM)hItem);
        }
        return FALSE;
    }
    /* ListView H-axis: use live column widths vs client width.
     * GetScrollInfo(SB_HORZ) nMax and nPage are unreliable after ShowScrollBar(FALSE)
     * and may not reflect column-resize changes made through the header control. */
    if (ctx->isListView && !vert) {
        int totalW = Msb_ListViewTotalColumnWidth(ctx->hTarget);
        RECT rcLV; GetClientRect(ctx->hTarget, &rcLV);
        return (totalW > rcLV.right);
    }
    /* ListView H-axis (and other cases): nPage==0 means no overflow. */
    if ((ctx->isTreeView || ctx->isListView) && si.nPage == 0)
        return FALSE;
    int scrollRange = (si.nMax - si.nMin) - (int)si.nPage;
    return (scrollRange > 0);
}

/* Show or hide the bar window depending on whether content overflows.
 * In NOHIDE mode the bar is always shown.  Returns TRUE if visible. */
static BOOL Msb_UpdateVisibility(MsbCtx* ctx)
{
    /* H-scroll Step 1: bar never shown. Native bar still suppressed by subclass. */
    if (ctx->flags & MSB_HORIZONTAL) {
        if (ctx->fadeState != FADE_INVISIBLE) {
            KillTimer(ctx->hBar, 3);
            ctx->fadeState = FADE_INVISIBLE;
            ctx->fadeWidth = 0.0f;
            ShowWindow(ctx->hBar, SW_HIDE);
        }
        return FALSE;
    }
    if (ctx->flags & MSB_NOHIDE) return TRUE;
    BOOL overflows = Msb_ContentOverflows(ctx);
    if (!overflows) {
        if (ctx->fadeState != FADE_INVISIBLE) {
            KillTimer(ctx->hBar, 3);
            ctx->fadeState = FADE_INVISIBLE;
            ctx->fadeWidth = 0.0f;
            ShowWindow(ctx->hBar, SW_HIDE);
            /* H-bar disappeared: let V-bar peer expand back to full height. */
            if ((ctx->flags & MSB_HORIZONTAL) && ctx->hTarget) {
                MsbCtx* ctxV = (MsbCtx*)GetPropW(ctx->hTarget, kMsbTargetPropV);
                if (ctxV && IsWindow(ctxV->hBar) && ctxV->fadeState != FADE_INVISIBLE) {
                    Msb_PositionBar(ctxV);
                    InvalidateRect(ctxV->hBar, NULL, FALSE);
                }
            }
        }
        return FALSE;
    }
    /* Content overflows — make sure the bar is visible at least as thin strip */
    if (ctx->fadeState == FADE_INVISIBLE) {
        ctx->fadeState = FADE_HIDDEN;
        ctx->fadeWidth = (float)S(ctx, MSB_WIDTH_HIDDEN);
        Msb_PositionBar(ctx);
        ShowWindow(ctx->hBar, SW_SHOWNOACTIVATE);
        /* H-bar just appeared: shorten the V-bar peer so they stay flush. */
        if ((ctx->flags & MSB_HORIZONTAL) && ctx->hTarget) {
            MsbCtx* ctxV = (MsbCtx*)GetPropW(ctx->hTarget, kMsbTargetPropV);
            if (ctxV && IsWindow(ctxV->hBar) && ctxV->fadeState != FADE_INVISIBLE) {
                Msb_PositionBar(ctxV);
                InvalidateRect(ctxV->hBar, NULL, FALSE);
            }
        }
    }
    return TRUE;
}

/*
 * Msb_UpdateVisibilityGuarded — like Msb_UpdateVisibility, but for
 * TreeView/ListView targets it suppresses spurious HIDDEN→INVISIBLE
 * transitions that arise from transient zero-range scroll events (e.g.
 * auto-scroll-to-selection), while still permitting INVISIBLE→HIDDEN when
 * content genuinely starts overflowing for the first time.
 */
static BOOL Msb_UpdateVisibilityGuarded(MsbCtx* ctx)
{
    BOOL wasNonInvisible = (ctx->fadeState != FADE_INVISIBLE);
    BOOL result = Msb_UpdateVisibility(ctx);
    /* H-scroll Step 1: never restore the H-bar. */
    if (ctx->flags & MSB_HORIZONTAL) return FALSE;
    if ((ctx->isListView || ctx->isTreeView) && wasNonInvisible && !result) {
        /* Content appeared to fit — likely a transient artefact of an
         * internal scroll-to-selection event (GetScrollInfo range == 0 briefly).
         * Restore hint-strip visibility instead of hiding the bar. */
        ctx->fadeState = FADE_HIDDEN;
        ctx->fadeWidth = (float)S(ctx, MSB_WIDTH_HIDDEN);
        ShowWindow(ctx->hBar, SW_SHOWNOACTIVATE);
        result = TRUE;
    }
    return result;
}

/* ── Layout calculation ─────────────────────────────────────────────────────*/

/*
 * Msb_Layout — (re)compute all rects from the current bar window size
 * and the target's current SCROLLINFO.  Called on every WM_SIZE of the bar.
 */
static void Msb_Layout(MsbCtx* ctx)
{
    RECT rc;
    GetClientRect(ctx->hBar, &rc);
    int w = rc.right;
    int h = rc.bottom;

    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
    int arrowLen = S(ctx, MSB_ARROW_HEIGHT);

    if (vert) {
        /* top arrow */
        ctx->rArrowUp.left   = 0;
        ctx->rArrowUp.top    = 0;
        ctx->rArrowUp.right  = w;
        ctx->rArrowUp.bottom = arrowLen;

        /* bottom arrow */
        ctx->rArrowDown.left   = 0;
        ctx->rArrowDown.top    = h - arrowLen;
        ctx->rArrowDown.right  = w;
        ctx->rArrowDown.bottom = h;

        /* track */
        ctx->rTrack.left   = 0;
        ctx->rTrack.top    = arrowLen;
        ctx->rTrack.right  = w;
        ctx->rTrack.bottom = h - arrowLen;
    } else {
        /* left arrow */
        ctx->rArrowUp.left   = 0;
        ctx->rArrowUp.top    = 0;
        ctx->rArrowUp.right  = arrowLen;
        ctx->rArrowUp.bottom = h;

        /* right arrow */
        ctx->rArrowDown.left   = w - arrowLen;
        ctx->rArrowDown.top    = 0;
        ctx->rArrowDown.right  = w;
        ctx->rArrowDown.bottom = h;

        /* track */
        ctx->rTrack.left   = arrowLen;
        ctx->rTrack.top    = 0;
        ctx->rTrack.right  = w - arrowLen;
        ctx->rTrack.bottom = h;
    }

    /* Thumb position from SCROLLINFO */
    int sbType = vert ? SB_VERT : SB_HORZ;
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(ctx->hTarget, sbType, &si);

    /* RichEdit stops updating nPos in SCROLLINFO when its native bar is hidden.
     * EM_GETSCROLLPOS always reflects the real internal scroll position. */
    if (ctx->isRichEdit) {
        POINT pt = {0, 0};
        SendMessageW(ctx->hTarget, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
        si.nPos = vert ? pt.y : pt.x;
    }
    /* ListView H-axis: override nMax/nPage with live column widths / client width.
     * GetScrollInfo(SB_HORZ) nMax and nPage are unreliable — they may be zero
     * (zeroed by ShowScrollBar(FALSE)) or in wrong units, making the thumb tiny. */
    else if (ctx->isListView && !vert) {
        Msb_FixListViewHScrollInfo(ctx->hTarget, &si);
        si.nPos = ctx->lvHPos;  /* authoritative — GetScrollInfo.nPos is stale */
    }
    /* For non-RichEdit controls (ListView, TreeView, …) the control may not
     * update GetScrollInfo.nPos until SB_THUMBPOSITION is committed.  During
     * drag we override nPos with the value we computed so the thumb tracks the
     * cursor in real time rather than lagging one frame behind. */
    else if (ctx->dragging) {
        si.nPos = ctx->dragCurPos;
    }

    int trackLen = vert ? (ctx->rTrack.bottom - ctx->rTrack.top)
                        : (ctx->rTrack.right  - ctx->rTrack.left);
    int thumbMin = S(ctx, MSB_THUMB_MIN);

    /* RichEdit: override nPage and (for horizontal) nMax with real values.
     *   Vertical  : nPage = visible client height (nMax from SCROLLINFO is correct).
     *   Horizontal: nMax  = actual content width measured from line positions;
     *               nPage = visible client width.
     * Without this the horizontal thumb reflects the fixed EM_SETTARGETDEVICE
     * line width (32767 px) rather than the real text extent. */
    if (ctx->isRichEdit) {
        RECT rcT;
        GetClientRect(ctx->hTarget, &rcT);
        int clientLen = vert ? rcT.bottom : rcT.right;
        /* Vertical bar: subtract the horizontal peer bar's height from the
         * viewport size so the thumb correctly reflects content vs visible area.
         * (The H bar overlaps the bottom of the target, so that many px of
         * content are hidden behind it.) */
        if (vert) {
            MsbCtx* ctxH = (MsbCtx*)GetPropW(ctx->hTarget, kMsbTargetPropH);
            if (ctxH && IsWindow(ctxH->hBar) && ctxH->fadeState != FADE_INVISIBLE) {
                int hBarH = S(ctxH, MSB_WIDTH_FULL);
                /* Match the format rect shrinkage in Msb_ApplyRichFormatRect:
                 * nPage = visible content height = ch - hBarH - MSB_VERT_MARGIN. */
                clientLen -= hBarH + S(ctx, MSB_VERT_MARGIN);
                if (clientLen < 1) clientLen = 1;
            }
            /* EM_SHOWSCROLLBAR(FALSE) stops RichEdit maintaining GetScrollInfo,
             * so si.nMax is stale.  Override with directly measured doc height. */
            if (ctx->richVertMax <= 0)
                ctx->richVertMax = Msb_MeasureRichVertMax(ctx->hTarget);
            if (ctx->richVertMax > 0) {
                si.nMax = ctx->richVertMax;
                si.nMin = 0;
            }
        }
        if (clientLen > 0)
            si.nPage = (UINT)clientLen;
        if (!vert) {
            /* Use cached content width; re-measure if not yet set. */
            if (ctx->richHorzMax <= 0)
                ctx->richHorzMax = Msb_MeasureRichHorzMax(ctx->hTarget);
            int contentW = ctx->richHorzMax;
            /* Add a small margin so the last character isn't clipped. */
            contentW += S(ctx, 24);
            /* nMax must be at least nPage (otherwise no scrolling needed). */
            if (contentW < clientLen)
                contentW = clientLen;
            si.nMax = contentW;
            si.nMin = 0;
        }
    }

    int range = si.nMax - si.nMin + 1;
    int thumbLen = (range > 0)
        ? max(thumbMin, (int)((LONGLONG)trackLen * si.nPage / range))
        : trackLen;
    if (thumbLen > trackLen) thumbLen = trackLen;

    int scrollRange = range - (int)si.nPage;
    int thumbTravel = trackLen - thumbLen;
    int thumbOffset = (scrollRange > 0 && thumbTravel > 0)
        ? (int)((LONGLONG)thumbTravel * (si.nPos - si.nMin) / scrollRange)
        : 0;

    if (vert) {
        ctx->rThumb.left   = 0;
        ctx->rThumb.top    = ctx->rTrack.top + thumbOffset;
        ctx->rThumb.right  = w;
        ctx->rThumb.bottom = ctx->rTrack.top + thumbOffset + thumbLen;
    } else {
        ctx->rThumb.left   = ctx->rTrack.left + thumbOffset;
        ctx->rThumb.top    = 0;
        ctx->rThumb.right  = ctx->rTrack.left + thumbOffset + thumbLen;
        ctx->rThumb.bottom = h;
    }
}

/* ── Drawing ─────────────────────────────────────────────────────────────────*/

/*
 * Fill a RECT with a rounded rectangle using a brush.
 * cr is the corner radius in pixels.
 */
static void DrawRoundRect(HDC hdc, const RECT* r, int cr, COLORREF fill, COLORREF border)
{
    HBRUSH hBr = CreateSolidBrush(fill);
    HPEN   hPn = CreatePen(PS_SOLID, 1, border);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
    HPEN   hOldPn = (HPEN)  SelectObject(hdc, hPn);
    RoundRect(hdc, r->left, r->top, r->right, r->bottom, cr, cr);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPn);
    DeleteObject(hBr);
    DeleteObject(hPn);
}

/*
 * Draw a triangle (arrow glyph) pointing in the given direction.
 * dir: 0=up, 1=down, 2=left, 3=right
 */
static void DrawArrowGlyph(HDC hdc, const RECT* r, int dir, COLORREF clr)
{
    int cx = (r->left + r->right)  / 2;
    int cy = (r->top  + r->bottom) / 2;
    int sz = max(3, (int)min(r->right - r->left, r->bottom - r->top) / 4);

    POINT pts[3];
    switch (dir) {
        case 0: /* up    */
            pts[0] = {cx,      cy - sz};
            pts[1] = {cx - sz, cy + sz};
            pts[2] = {cx + sz, cy + sz};
            break;
        case 1: /* down  */
            pts[0] = {cx,      cy + sz};
            pts[1] = {cx - sz, cy - sz};
            pts[2] = {cx + sz, cy - sz};
            break;
        case 2: /* left  */
            pts[0] = {cx - sz, cy};
            pts[1] = {cx + sz, cy - sz};
            pts[2] = {cx + sz, cy + sz};
            break;
        case 3: /* right */
            pts[0] = {cx + sz, cy};
            pts[1] = {cx - sz, cy - sz};
            pts[2] = {cx - sz, cy + sz};
            break;
        default:
            return;
    }

    HBRUSH hBr = CreateSolidBrush(clr);
    HPEN   hPn = CreatePen(PS_SOLID, 1, clr);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
    HPEN   hOldPn = (HPEN)  SelectObject(hdc, hPn);
    Polygon(hdc, pts, 3);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPn);
    DeleteObject(hBr);
    DeleteObject(hPn);
}

static void Msb_Paint(HWND hwnd, MsbCtx* ctx)
{
    PAINTSTRUCT ps;
    HDC hdcReal = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right;
    int h = rc.bottom;

    /* Off-screen buffer for flicker-free painting */
    HDC     hdc    = CreateCompatibleDC(hdcReal);
    HBITMAP hBmp   = CreateCompatibleBitmap(hdcReal, w, h);
    HBITMAP hOldBm = (HBITMAP)SelectObject(hdc, hBmp);

    int cr = S(ctx, MSB_CORNER_RADIUS);
    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);

    /* ── Background (track) ─────────────────────────────────────────────── */
    int  barThick   = vert ? w : h;
    /* In hint mode (bar at rest, showing 3px position indicator) use white so
     * only the thumb is visible against the editor background — no track box. */
    BOOL hintMode = (barThick <= S(ctx, MSB_WIDTH_HIDDEN));
    HBRUSH hBgBr = CreateSolidBrush(hintMode ? GetSysColor(COLOR_WINDOW) : MSB_CLR_TRACK);
    FillRect(hdc, &rc, hBgBr);
    DeleteObject(hBgBr);

    /* Arrows only when bar is sufficiently expanded (>= 5/8 of full width) */
    BOOL showArrows = (barThick >= S(ctx, MSB_WIDTH_FULL) * 5 / 8);

    if (showArrows) {
    COLORREF arrUpClr = (ctx->arrowUpState == ARROW_PRESSED) ? MSB_CLR_ARROW_BG_PRESS
                      : (ctx->arrowUpState == ARROW_HOVER)   ? MSB_CLR_ARROW_BG_HOVER
                                                              : MSB_CLR_ARROW_BG;
    /* Rounded cap only on the far end (top-left+top-right for up arrow) */
    DrawRoundRect(hdc, &ctx->rArrowUp,   cr * 2, arrUpClr, arrUpClr);
    DrawArrowGlyph(hdc, &ctx->rArrowUp,  vert ? 0 : 2, MSB_CLR_ARROW_GLYPH);

    /* ── Arrow down / right ──────────────────────────────────────────────── */
    COLORREF arrDnClr = (ctx->arrowDownState == ARROW_PRESSED) ? MSB_CLR_ARROW_BG_PRESS
                      : (ctx->arrowDownState == ARROW_HOVER)   ? MSB_CLR_ARROW_BG_HOVER
                                                                : MSB_CLR_ARROW_BG;
    DrawRoundRect(hdc, &ctx->rArrowDown, cr * 2, arrDnClr, arrDnClr);
    DrawArrowGlyph(hdc, &ctx->rArrowDown, vert ? 1 : 3, MSB_CLR_ARROW_GLYPH);
    } /* showArrows */

    COLORREF thumbClr = (ctx->thumbState == THUMB_DRAG)  ? MSB_CLR_THUMB_DRAG
                      : (ctx->thumbState == THUMB_HOVER) ? MSB_CLR_THUMB_HOVER
                                                         : MSB_CLR_THUMB;
    DrawRoundRect(hdc, &ctx->rThumb, cr * 2, thumbClr, thumbClr);

    /* Blit to screen */
    BitBlt(hdcReal, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, hOldBm);
    DeleteObject(hBmp);
    DeleteDC(hdc);

    EndPaint(hwnd, &ps);
}

/* ── Bar window proc ─────────────────────────────────────────────────────────*/

/* ListView H-scroll delivery — isolated for safe rewriting without risk to V. */
#include "my_scrollbar_hscroll.cpp"

static void Msb_ScrollToPos(MsbCtx* ctx, int newPos); /* forward decl */

/* Send a scroll message to the target and re-sync the bar. */
static void Msb_Scroll(MsbCtx* ctx, UINT cmd)
{
    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
    UINT msg  = vert ? WM_VSCROLL : WM_HSCROLL;

    if (ctx->isListView) {
        if (!vert) {
            /* ListView horizontal: route ALL H commands through Msb_ScrollToPos
             * (LVM_SCROLL + lvHPos tracking) so the position is always accurate.
             * WM_HSCROLL does not reliably update SCROLLINFO.nPos after
             * ShowScrollBar(FALSE) zeroes nMax, so we never trust it for nPos. */
            SCROLLINFO si = {sizeof(si), SIF_ALL};
            Msb_FixListViewHScrollInfo(ctx->hTarget, &si);
            int maxPos = max(0, (int)(si.nMax - (int)si.nPage + 1));
            /* Small fixed pixel step for arrow buttons; page = client width. */
            const int lineStep = S(ctx, 20);
            int newPos = ctx->lvHPos;
            switch (cmd) {
                case SB_LINELEFT:  newPos -= lineStep;       break;
                case SB_LINERIGHT: newPos += lineStep;       break;
                case SB_PAGELEFT:  newPos -= (int)si.nPage; break;
                case SB_PAGERIGHT: newPos += (int)si.nPage; break;
                default:           newPos  = ctx->lvHPos;   break;
            }
            if (newPos < 0)       newPos = 0;
            if (newPos > maxPos)  newPos = maxPos;
            Msb_ScrollToPos(ctx, newPos);
            /* Msb_ScrollToPos updated lvHPos and sent LVM_SCROLL.
             * The WM_HSCROLL interceptor will run but must NOT overwrite lvHPos.
             * Repaint the bar so the thumb reflects the new position. */
            Msb_Layout(ctx);
            InvalidateRect(ctx->hBar, NULL, FALSE);
            UpdateWindow(ctx->hBar);
            return;
        }
        /* Vertical: LVM_SCROLL rounds to row boundaries and works reliably. */
        RECT rcItem = {}; ListView_GetItemRect(ctx->hTarget, 0, &rcItem, LVIR_BOUNDS);
        int rowH = (rcItem.bottom > rcItem.top) ? (rcItem.bottom - rcItem.top) : S(ctx, 16);
        int dy = 0;
        switch (cmd) {
            case SB_LINEUP:   dy = -rowH; break;
            case SB_LINEDOWN: dy =  rowH; break;
            case SB_PAGEUP:  { RECT rc; GetClientRect(ctx->hTarget, &rc); dy = -rc.bottom; break; }
            case SB_PAGEDOWN: { RECT rc; GetClientRect(ctx->hTarget, &rc); dy =  rc.bottom; break; }
        }
        SendMessageW(ctx->hTarget, LVM_SCROLL, 0, (LPARAM)dy);
    } else {
        SendMessageW(ctx->hTarget, msg, MAKEWPARAM(cmd, 0), 0);
    }
    /* re-read SCROLLINFO and repaint */
    Msb_Layout(ctx);
    InvalidateRect(ctx->hBar, NULL, FALSE);
    UpdateWindow(ctx->hBar);
}

/* Scroll a ListView or TreeView to an absolute pixel position.
 * Reads current position from SCROLLINFO, computes the delta, and scrolls. */
static void Msb_ScrollToPos(MsbCtx* ctx, int newPos)
{
    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);

    if (ctx->isListView) {
        if (!vert) {
            /* ListView H-scroll: delivered by MsbH_DeliverScroll (my_scrollbar_hscroll.cpp).
             * All clamping and lvHPos tracking live there — edit that file to fix H bugs
             * without any risk of disturbing the vertical scrollbar path below. */
            MsbH_DeliverScroll(ctx, newPos);
            return;
        }
        /* Vertical: LVM_SCROLL with row-height conversion works reliably. */
        SCROLLINFO si = {sizeof(si), SIF_POS};
        GetScrollInfo(ctx->hTarget, SB_VERT, &si);
        int delta = newPos - si.nPos;
        RECT rcItem = {};
        ListView_GetItemRect(ctx->hTarget, 0, &rcItem, LVIR_BOUNDS);
        int rowH = (rcItem.bottom > rcItem.top) ? (rcItem.bottom - rcItem.top) : S(ctx, 16);
        if (delta) SendMessageW(ctx->hTarget, LVM_SCROLL, 0, (LPARAM)(delta * rowH));
    } else {
        /* TreeView and other controls: SetScrollInfo + WM_VSCROLL/HSCROLL. */
        if (!vert) {
            /* H-scroll: routed through MsbH_DeliverScroll (my_scrollbar_hscroll.cpp).
             * Step 1: suppressed — content does not scroll yet. */
            MsbH_DeliverScroll(ctx, newPos);
            return;
        }
        SCROLLINFO setSi = {sizeof(SCROLLINFO), SIF_POS};
        setSi.nPos = newPos;
        SetScrollInfo(ctx->hTarget, vert ? SB_VERT : SB_HORZ, &setSi, FALSE);
        UINT smsg = vert ? WM_VSCROLL : WM_HSCROLL;
        SendMessageW(ctx->hTarget, smsg, MAKEWPARAM(SB_THUMBPOSITION, (WORD)newPos), 0);
        SendMessageW(ctx->hTarget, smsg, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    }
}

/* Hit-test a point against the bar zones.
 * Returns: 1=arrowUp, 2=arrowDown, 3=trackAbove, 4=trackBelow, 5=thumb, 0=none */
static int Msb_HitTest(MsbCtx* ctx, int x, int y)
{
    POINT pt = {x, y};
    if (PtInRect(&ctx->rArrowUp,   pt)) return 1;
    if (PtInRect(&ctx->rArrowDown, pt)) return 2;
    if (PtInRect(&ctx->rThumb,     pt)) return 5;
    /* track above thumb */
    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
    if (vert) {
        if (y >= ctx->rTrack.top  && y < ctx->rThumb.top)    return 3;
        if (y >  ctx->rThumb.bottom && y <= ctx->rTrack.bottom) return 4;
    } else {
        if (x >= ctx->rTrack.left && x < ctx->rThumb.left)   return 3;
        if (x >  ctx->rThumb.right  && x <= ctx->rTrack.right)  return 4;
    }
    return 0;
}

static LRESULT CALLBACK Msb_BarWndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam)
{
    MsbCtx* ctx = (MsbCtx*)GetPropW(hwnd, kMsbBarProp);

    switch (msg) {
        case WM_SIZE:
            if (ctx) {
                Msb_Layout(ctx);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_PAINT:
            if (ctx) {
                Msb_Paint(hwnd, ctx);
                return 0;
            }
            break;

        case WM_ERASEBKGND:
            return 1;

        /* ── Mouse interaction (Phase 2) ──────────────────────────────── */

        case WM_LBUTTONDOWN: {
            if (!ctx) break;
            KillTimer(hwnd, 4); /* cancel any pending leave-delay contraction */
            /* For ListView H-axis: re-sync lvHPos from the header control RIGHT
             * NOW, before Msb_PositionBar is called below.
             * At click time no scroll is in progress: the user has just moved the
             * mouse and stopped, so the header item rects are fully settled and
             * Msb_GetListViewHPos returns the ground-truth offset.
             * This guards against any stale lvHPos caused by paths we don't
             * intercept (e.g. LVM_DELETEALLITEMS not resetting H-scroll on all
             * Windows versions, or WM_SIZE firing in an un-guarded context).
             * Reading BEFORE Msb_PositionBar avoids any header state changes
             * that SetWindowPos on the bar sibling might trigger. */
            if (ctx->isListView && (ctx->flags & MSB_HORIZONTAL)) {
                ctx->lvHPos = Msb_GetListViewHPos(ctx->hTarget);
            }
            /* If the bar is in hint-strip state (3 px), jump to full width
             * immediately so the user can hit-test arrows/track/thumb now.*/
            if (!(ctx->flags & MSB_NOHIDE) && ctx->fadeState == FADE_HIDDEN) {
                KillTimer(hwnd, 3);
                ctx->fadeWidth = (float)S(ctx, MSB_WIDTH_FULL);
                ctx->fadeState = FADE_VISIBLE;
                Msb_PositionBar(ctx);
                Msb_Layout(ctx);
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindow(hwnd);
            }
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            int hit = Msb_HitTest(ctx, x, y);
            if (!hit) break;

            SetCapture(hwnd);

            if (hit == 5) {
                /* Thumb drag start */
                ctx->dragging    = TRUE;
                ctx->thumbState  = THUMB_DRAG;
                BOOL vert        = !(ctx->flags & MSB_HORIZONTAL);
                ctx->dragStartPx = vert ? y : x;
                int startPos = 0;
                if (ctx->isRichEdit) {
                    POINT pt = {0, 0};
                    SendMessageW(ctx->hTarget, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
                    startPos = vert ? pt.y : pt.x;
                } else if (ctx->isListView && !vert) {
                    /* Read true position from header — GetScrollInfo.nPos and
                     * lvHPos can both be stale for ListView H-axis. */
                    startPos = Msb_GetListViewHPos(ctx->hTarget);
                    ctx->lvHPos = startPos;  /* sync so Msb_Layout draws thumb correctly */
                } else {
                    SCROLLINFO si = {sizeof(si), SIF_POS};
                    GetScrollInfo(ctx->hTarget,
                                  vert ? SB_VERT : SB_HORZ, &si);
                    startPos = si.nPos;
                }
                ctx->dragStartPos = startPos;
                ctx->dragCurPos   = startPos;
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (hit == 1 || hit == 2) {
                /* Arrow click — thumb turns pink, arrow highlights, auto-repeat */
                BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
                UINT cmd  = (hit == 1) ? (UINT)(vert ? SB_LINEUP   : SB_LINELEFT)
                                       : (UINT)(vert ? SB_LINEDOWN  : SB_LINERIGHT);
                if (hit == 1) ctx->arrowUpState   = ARROW_PRESSED;
                if (hit == 2) ctx->arrowDownState = ARROW_PRESSED;
                ctx->thumbState = THUMB_DRAG;
                ctx->repeatCmd  = cmd;
                ctx->timerFirst = TRUE;
                Msb_Scroll(ctx, cmd);
                ctx->timerId = SetTimer(hwnd, 1, 350, NULL);
            } else {
                /* Track click (hit 3 or 4) — jump so thumb centre lands on click.
                 * Maps the click pixel to a fraction of the thumb-travelable
                 * track, then scrolls directly there.  No auto-repeat. */
                BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
                int clickPx    = vert ? y : x;
                int trackStart = vert ? ctx->rTrack.top    : ctx->rTrack.left;
                int trackLen   = vert ? (ctx->rTrack.bottom - ctx->rTrack.top)
                                      : (ctx->rTrack.right  - ctx->rTrack.left);
                int thumbLen   = vert ? (ctx->rThumb.bottom - ctx->rThumb.top)
                                      : (ctx->rThumb.right  - ctx->rThumb.left);
                int travelPx   = max(1, trackLen - thumbLen);
                int offset     = clickPx - trackStart - thumbLen / 2;
                offset = max(0, min(travelPx, offset));

                /* Get scroll range, fixing up nPage and (for horiz RichEdit) nMax.
                 * RichEdit reports nMax = 32767 (EM_SETTARGETDEVICE line width);
                 * override with the real measured content width, same as Msb_Layout. */
                SCROLLINFO si = {sizeof(si), SIF_ALL};
                GetScrollInfo(ctx->hTarget, vert ? SB_VERT : SB_HORZ, &si);
                if (ctx->isRichEdit) {
                    RECT rcT; GetClientRect(ctx->hTarget, &rcT);
                    int clientLen = vert ? rcT.bottom : rcT.right;
                    si.nPage = (UINT)clientLen;
                    if (vert) {
                        if (ctx->richVertMax <= 0)
                            ctx->richVertMax = Msb_MeasureRichVertMax(ctx->hTarget);
                        if (ctx->richVertMax > 0) { si.nMax = ctx->richVertMax; si.nMin = 0; }
                    } else {
                        if (ctx->richHorzMax <= 0)
                            ctx->richHorzMax = Msb_MeasureRichHorzMax(ctx->hTarget);
                        int contentW = ctx->richHorzMax + S(ctx, 24);
                        if (contentW < clientLen) contentW = clientLen;
                        si.nMax = contentW;
                        si.nMin = 0;
                    }
                } else if (ctx->isListView && !vert) {
                    /* ListView H-axis: live column-width / client-width measurement,
                     * same as Msb_Layout, so track-click lands correctly. */
                    Msb_FixListViewHScrollInfo(ctx->hTarget, &si);
                }
                int scrollRange = (si.nMax - si.nMin) - (int)si.nPage + 1;
                if (scrollRange < 1) scrollRange = 1;

                int newPos = si.nMin
                           + (int)((float)offset / travelPx * scrollRange + 0.5f);
                newPos = max(si.nMin,
                             min(si.nMax - (int)si.nPage + 1, newPos));

                if (ctx->isRichEdit) {
                    POINT pt = {0, 0};
                    SendMessageW(ctx->hTarget, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
                    if (vert) pt.y = newPos; else pt.x = newPos;
                    SendMessageW(ctx->hTarget, EM_SETSCROLLPOS, 0, (LPARAM)&pt);
                } else {
                    Msb_ScrollToPos(ctx, newPos);
                }
                ctx->thumbState = THUMB_DRAG;
                Msb_Layout(ctx);
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindow(hwnd);
                /* Brief pink flash, then reset colour after 400 ms */
                SetTimer(hwnd, 2, 400, NULL);
                ReleaseCapture();
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!ctx) break;
            /* Mouse re-entered bar — cancel any pending leave-delay. */
            KillTimer(hwnd, 4);
            /* Expand on cursor entry when bar is in hidden or contracting state. */
            if (!(ctx->flags & MSB_NOHIDE) &&
                (ctx->fadeState == FADE_HIDDEN || ctx->fadeState == FADE_CONTRACTING)) {
                KillTimer(hwnd, 3);
                ctx->fadeState = FADE_EXPANDING;
                SetTimer(hwnd, 3, 16, NULL);
            }
            if (ctx->dragging) {
                /* Thumb drag — map cursor delta to scroll position */
                BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
                int cur   = vert ? GET_Y_LPARAM(lParam) : GET_X_LPARAM(lParam);
                int delta = cur - ctx->dragStartPx;

                SCROLLINFO si = {sizeof(si), SIF_ALL};
                GetScrollInfo(ctx->hTarget, vert ? SB_VERT : SB_HORZ, &si);
                /* Fix nPage and nMax for RichEdit (nMax is stale when native bar hidden). */
                if (ctx->isRichEdit) {
                    RECT rcT; GetClientRect(ctx->hTarget, &rcT);
                    int clientLen = vert ? rcT.bottom : rcT.right;
                    si.nPage = (UINT)clientLen;
                    if (vert) {
                        if (ctx->richVertMax <= 0)
                            ctx->richVertMax = Msb_MeasureRichVertMax(ctx->hTarget);
                        if (ctx->richVertMax > 0) { si.nMax = ctx->richVertMax; si.nMin = 0; }
                    } else {
                        if (ctx->richHorzMax <= 0)
                            ctx->richHorzMax = Msb_MeasureRichHorzMax(ctx->hTarget);
                        int contentW = ctx->richHorzMax + S(ctx, 24);
                        if (contentW < clientLen) contentW = clientLen;
                        si.nMax = contentW;
                        si.nMin = 0;
                    }
                } else if (ctx->isListView && !vert) {
                    /* ListView H-axis: live column-width / client-width. */
                    Msb_FixListViewHScrollInfo(ctx->hTarget, &si);
                }
                int trackPx = vert ? (ctx->rTrack.bottom - ctx->rTrack.top)
                                   : (ctx->rTrack.right  - ctx->rTrack.left);
                int thumbPx = vert ? (ctx->rThumb.bottom - ctx->rThumb.top)
                                   : (ctx->rThumb.right  - ctx->rThumb.left);
                int range   = si.nMax - si.nMin - (int)si.nPage + 1;
                if (range <= 0) break;

                int newPos = ctx->dragStartPos
                           + (int)(delta * (float)range / max(1, trackPx - thumbPx) + 0.5f);
                newPos = max(si.nMin, min(si.nMax - (int)si.nPage + 1, newPos));
                ctx->dragCurPos = newPos;

                if (ctx->isRichEdit) {
                    UINT smsg = vert ? WM_VSCROLL : WM_HSCROLL;
                    SendMessageW(ctx->hTarget, smsg,
                                 MAKEWPARAM(SB_THUMBTRACK, (WORD)newPos), 0);
                } else if (ctx->isListView) {
                    /* ListView scrolls via LVM_SCROLL (pixel delta) —
                     * SetScrollInfo+SB_THUMBTRACK zeroes the delta because
                     * ListView reads nPos AFTER we already updated it. */
                    Msb_ScrollToPos(ctx, newPos);
                } else {
                    /* For ListView/TreeView: set the SCROLLINFO position first so
                     * the control knows the target row/pixel, then send
                     * SB_THUMBTRACK (the live-drag notification) so the control
                     * scrolls continuously while the thumb is being dragged.
                     * SB_THUMBPOSITION is sent on WM_LBUTTONUP (final commit). */
                    SCROLLINFO setSi = {sizeof(SCROLLINFO), SIF_POS};
                    setSi.nPos = newPos;
                    SetScrollInfo(ctx->hTarget, vert ? SB_VERT : SB_HORZ, &setSi, FALSE);
                    UINT smsg = vert ? WM_VSCROLL : WM_HSCROLL;
                    SendMessageW(ctx->hTarget, smsg,
                                 MAKEWPARAM(SB_THUMBTRACK, (WORD)newPos), 0);
                }
                Msb_Layout(ctx);
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindow(hwnd);
                return 0;
            }

            /* ── Hover-state tracking (not dragging) ─────────────────── */
            {
                int x   = GET_X_LPARAM(lParam);
                int y   = GET_Y_LPARAM(lParam);
                int hit = Msb_HitTest(ctx, x, y);

                ThumbState newThumb = (hit == 5) ? THUMB_HOVER : THUMB_NORMAL;
                ArrowState newUp    = (hit == 1) ? ARROW_HOVER : ARROW_NORMAL;
                ArrowState newDown  = (hit == 2) ? ARROW_HOVER : ARROW_NORMAL;

                BOOL changed = (newThumb        != ctx->thumbState    ||
                                newUp           != ctx->arrowUpState  ||
                                newDown         != ctx->arrowDownState);
                ctx->thumbState     = newThumb;
                ctx->arrowUpState   = newUp;
                ctx->arrowDownState = newDown;

                /* Register for WM_MOUSELEAVE so we know when the cursor exits */
                if (!ctx->trackingMouse) {
                    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
                    TrackMouseEvent(&tme);
                    ctx->trackingMouse = TRUE;
                }

                if (changed) InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (!ctx) break;
            ctx->trackingMouse  = FALSE;
            /* Don't reset thumb state or start contraction while thumb is
             * being dragged — the cursor leaving the bar strip is expected. */
            if (ctx->dragging) return 0;
            ctx->thumbState     = THUMB_NORMAL;
            ctx->arrowUpState   = ARROW_NORMAL;
            ctx->arrowDownState = ARROW_NORMAL;
            InvalidateRect(hwnd, NULL, FALSE);
            /* Arm a 200 ms leave-delay (timer 4) before contracting.
             * The animation itself starts when timer 4 fires. */
            if (!(ctx->flags & MSB_NOHIDE) &&
                (ctx->fadeState == FADE_VISIBLE || ctx->fadeState == FADE_EXPANDING)) {
                KillTimer(hwnd, 3);
                SetTimer(hwnd, 4, 200, NULL);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!ctx) break;
            if (ctx->timerId) {
                KillTimer(hwnd, ctx->timerId);
                ctx->timerId = 0;
            }
            if (ctx->dragging) {
                ctx->dragging = FALSE;
                BOOL vert     = !(ctx->flags & MSB_HORIZONTAL);
                if (ctx->isListView) {
                    /* Commit final drag position via LVM_SCROLL delta — same
                     * path as live drag, avoids SB_THUMBPOSITION row/pixel
                     * confusion on ListView. */
                    Msb_ScrollToPos(ctx, ctx->dragCurPos);
                } else {
                    UINT smsg = vert ? WM_VSCROLL : WM_HSCROLL;
                    SendMessageW(ctx->hTarget, smsg,
                                 MAKEWPARAM(SB_THUMBPOSITION, (WORD)ctx->dragCurPos), 0);
                    SendMessageW(ctx->hTarget, smsg, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
                }
                Msb_Layout(ctx);
            }
            /* Reset arrow/thumb states; restore hover if cursor is still in bar */
            ctx->arrowUpState   = ARROW_NORMAL;
            ctx->arrowDownState = ARROW_NORMAL;
            {
                POINT cur; GetCursorPos(&cur); ScreenToClient(hwnd, &cur);
                RECT  rcC; GetClientRect(hwnd, &rcC);
                if (PtInRect(&rcC, cur)) {
                    ctx->thumbState = THUMB_HOVER;
                    int h = Msb_HitTest(ctx, cur.x, cur.y);
                    if (h == 1) ctx->arrowUpState   = ARROW_HOVER;
                    if (h == 2) ctx->arrowDownState = ARROW_HOVER;
                } else {
                    ctx->thumbState = THUMB_NORMAL;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            ReleaseCapture();
            /* If cursor left the bar during the interaction, start fade-out */
            if (!(ctx->flags & MSB_NOHIDE) &&
                (ctx->fadeState == FADE_VISIBLE || ctx->fadeState == FADE_EXPANDING)) {
                POINT cur; GetCursorPos(&cur); ScreenToClient(hwnd, &cur);
                RECT  rcC; GetClientRect(hwnd, &rcC);
                if (!PtInRect(&rcC, cur)) {
                    KillTimer(hwnd, 3);
                    ctx->fadeState = FADE_CONTRACTING;
                    SetTimer(hwnd, 3, 16, NULL);
                }
            }
            return 0;
        }

        case WM_TIMER: {
            if (!ctx) break;
            if (wParam == 4) {
                /* Leave-delay expired: start the actual contraction animation. */
                KillTimer(hwnd, 4);
                /* Don't contract while dragging — WM_LBUTTONUP will handle it. */
                if (ctx->dragging) return 0;
                if (ctx && !(ctx->flags & MSB_NOHIDE) &&
                    (ctx->fadeState == FADE_VISIBLE || ctx->fadeState == FADE_EXPANDING ||
                     ctx->fadeState == FADE_HIDDEN)) {
                    ctx->fadeState = FADE_CONTRACTING;
                    SetTimer(hwnd, 3, 16, NULL);
                }
                return 0;
            }
            if (wParam == 2) {
                /* Wheel-idle reset: wheel has been quiet for 220 ms */
                KillTimer(hwnd, 2);
                POINT cur; GetCursorPos(&cur); ScreenToClient(hwnd, &cur);
                RECT  rcC; GetClientRect(hwnd, &rcC);
                BOOL overBar = PtInRect(&rcC, cur);
                /* Reset thumb colour if no other interaction is active */
                if (!ctx->dragging && ctx->timerId == 0) {
                    ctx->thumbState = overBar ? THUMB_HOVER : THUMB_NORMAL;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                /* Contract if bar was expanded by wheel and cursor has since left */
                if (!(ctx->flags & MSB_NOHIDE) && !overBar &&
                    (ctx->fadeState == FADE_VISIBLE || ctx->fadeState == FADE_EXPANDING)) {
                    KillTimer(hwnd, 3);
                    ctx->fadeState = FADE_CONTRACTING;
                    SetTimer(hwnd, 3, 16, NULL);
                }
                return 0;
            }
            if (wParam == 3) {
                /* Fade animation tick — expand or contract the bar width */
                float fullW   = (float)S(ctx, MSB_WIDTH_FULL);
                float hiddenW = (float)S(ctx, MSB_WIDTH_HIDDEN);
                float step    = (fullW - hiddenW) / 4.5f;  /* ~75 ms at 16 ms/tick */
                if (ctx->fadeState == FADE_EXPANDING) {
                    ctx->fadeWidth += step;
                    if (ctx->fadeWidth >= fullW) {
                        ctx->fadeWidth = fullW;
                        ctx->fadeState = FADE_VISIBLE;
                        KillTimer(hwnd, 3);
                    }
                } else if (ctx->fadeState == FADE_CONTRACTING) {
                    ctx->fadeWidth -= step;
                    if (ctx->fadeWidth <= hiddenW) {
                        /* Settle at hint-strip width — bar stays visible as
                         * 3px position indicator while content overflows. */
                        ctx->fadeWidth = hiddenW;
                        ctx->fadeState = FADE_HIDDEN;
                        KillTimer(hwnd, 3);
                    }
                }
                Msb_PositionBar(ctx);
                /* H-bar animating: keep V-bar peer flush with H-bar top. */
                if ((ctx->flags & MSB_HORIZONTAL) && ctx->hTarget) {
                    MsbCtx* ctxV = (MsbCtx*)GetPropW(ctx->hTarget, kMsbTargetPropV);
                    if (ctxV && IsWindow(ctxV->hBar) && ctxV->fadeState != FADE_INVISIBLE) {
                        Msb_PositionBar(ctxV);
                        InvalidateRect(ctxV->hBar, NULL, FALSE);
                    }
                }
                Msb_Layout(ctx);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (wParam != 1) break;
            if (ctx->timerFirst) {
                /* Switch to fast auto-repeat rate */
                ctx->timerFirst = FALSE;
                KillTimer(hwnd, ctx->timerId);
                ctx->timerId = SetTimer(hwnd, 1, 50, NULL);
            }
            Msb_Scroll(ctx, ctx->repeatCmd);
            return 0;
        }

        case WM_CAPTURECHANGED:
            if (ctx && ctx->timerId) {
                KillTimer(hwnd, ctx->timerId);
                ctx->timerId = 0;
            }
            if (ctx) KillTimer(hwnd, 4); /* cancel any leave-delay too */
            if (ctx) {
                ctx->dragging       = FALSE;
                ctx->thumbState     = THUMB_NORMAL;
                ctx->arrowUpState   = ARROW_NORMAL;
                ctx->arrowDownState = ARROW_NORMAL;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ── Format-rect helper ─────────────────────────────────────────────────────*/

/*
 * Msb_ApplyRichFormatRect — shrink the RichEdit formatting rectangle so the
 * last line of text is not hidden behind the horizontal scrollbar.
 *
 * RichEdit clamps WM_VSCROLL to (nMax - clientHeight), so even if we report a
 * larger nPage in Msb_Layout the control itself won't scroll past that point.
 * EM_SETRECT tells RichEdit its usable height is (clientHeight - hBarH), which
 * shifts its internal max-scroll to (nMax - (clientHeight - hBarH)), allowing
 * the last line to scroll fully above the H bar.
 */
static void Msb_ApplyRichFormatRect(HWND hTarget)
{
    MsbCtx* ctxV = (MsbCtx*)GetPropW(hTarget, kMsbTargetPropV);
    MsbCtx* ctxH = (MsbCtx*)GetPropW(hTarget, kMsbTargetPropH);
    MsbCtx* any  = ctxV ? ctxV : ctxH;
    if (!any || !any->isRichEdit) return;

    RECT rc;
    GetClientRect(hTarget, &rc);
    /* Only shrink the bottom (H bar).  Do NOT shrink the right (V bar):
     * for non-wrapping RichEdit the right edge clips text unnecessarily;
     * horizontal scrolling handles right-side visibility instead. */
    if (ctxH && ctxH->fadeState != FADE_INVISIBLE) {
        int hBarH = S(ctxH, MSB_WIDTH_FULL);
        /* Subtract an additional MSB_VERT_MARGIN pixels so RichEdit leaves
         * visible empty space between the last line and the H bar.
         * The region (rc.bottom .. ch - hBarH) is outside the format rect so
         * RichEdit fills it with its background colour, giving breathing room. */
        rc.bottom = max(1, (int)(rc.bottom) - hBarH - S(any, MSB_VERT_MARGIN));
    }
    SendMessageW(hTarget, EM_SETRECT, 0, (LPARAM)&rc);
}

/* ── Target window subclass proc ────────────────────────────────────────────*/

static LRESULT CALLBACK Msb_TargetSubclassProc(HWND hwnd, UINT msg,
                                                WPARAM wParam, LPARAM lParam)
{
    MsbCtx* ctxV = (MsbCtx*)GetPropW(hwnd, kMsbTargetPropV);
    MsbCtx* ctxH = (MsbCtx*)GetPropW(hwnd, kMsbTargetPropH);
    MsbCtx* any  = ctxV ? ctxV : ctxH;   /* either one holds origProc */
    if (!any) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_NCPAINT: {
            /* TreeView/ListView re-show native scrollbars inside their WM_NCPAINT
             * handler every time they repaint their NC area (borders + bars).
             * Intercept: let origProc paint, then immediately suppress native bars.
             * inNcPaint guard breaks the loop: our ShowScrollBar(FALSE) call sends
             * another WM_NCPAINT which we let pass through directly, so the bar is
             * painted in the hidden state on the second pass. */
            if ((ctxV && ctxV->inNcPaint) || (ctxH && ctxH->inNcPaint))
                return CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            if (ctxV) ctxV->inNcPaint = TRUE;
            if (ctxH) ctxH->inNcPaint = TRUE;
            /* Guard WM_SIZE too: the ListView/TreeView WM_NCPAINT handler calls
             * ShowScrollBar(TRUE) which changes the client area and fires a
             * synchronous WM_SIZE.  Without this guard our WM_SIZE handler runs
             * fully (calls HideNativeBar → ShowScrollBar(FALSE) → another WM_SIZE
             * → another client-area change) on every scroll step → jitter. */
            if (ctxV) ctxV->inWmSize = TRUE;
            if (ctxH) ctxH->inWmSize = TRUE;
            LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            if (ctxV) ctxV->inWmSize = FALSE;
            if (ctxH) ctxH->inWmSize = FALSE;
            /* Capture H SCROLLINFO AFTER origProc — origProc updates nPos to the
             * current scroll position (e.g. after an LVM_SCROLL-triggered repaint).
             * ShowScrollBar(SB_HORZ, FALSE) resets nMax/nPage/nPos to 0 for
             * ListView/TreeView; restoring keeps the correct position so that the
             * next Msb_ScrollToPos delta is accurate and Msb_Layout shows the
             * thumb in the right place. */
            SCROLLINFO siH = {sizeof(siH)};
            BOOL restoreH = FALSE;
            if (ctxH && (ctxH->isListView || ctxH->isTreeView)) {
                siH.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
                GetScrollInfo(hwnd, SB_HORZ, &siH);
                restoreH = (siH.nMax > siH.nMin);
                /* Do NOT sync lvHPos here: WM_NCPAINT fires synchronously
                 * inside LVM_SCROLL BEFORE the ListView repositions the header
                 * control, so Msb_GetListViewHPos() would return the old value
                 * and corrupt lvHPos.  lvHPos is managed by Msb_ScrollToPos
                 * (during scrolling) and msb_reposition/WM_SIZE (on layout
                 * changes such as column resize and window resize). */
            }
            if (ctxV) Msb_HideNativeBar(hwnd, ctxV->isRichEdit, TRUE);
            /* Skip H hide during LVM_SCROLL delivery — ShowScrollBar(FALSE)
             * zeroes the ListView's internal scroll counter, which causes
             * LVM_SCROLL to clamp all leftward (negative) deltas to zero. */
            if (ctxH && !ctxH->inHDeliver) Msb_HideNativeBar(hwnd, ctxH->isRichEdit, FALSE);
            /* Reconstruct V for TreeView; restore H for ListView/TreeView. */
            if (ctxV && ctxV->isTreeView) Msb_ReconstructTreeVScrollInfo(hwnd);
            if (restoreH) SetScrollInfo(hwnd, SB_HORZ, &siH, FALSE);
            if (ctxV) ctxV->inNcPaint = FALSE;
            if (ctxH) ctxH->inNcPaint = FALSE;
            return r;
        }

        case WM_SIZE: {
            /* Re-entrancy guard: Msb_HideNativeBar calls ShowScrollBar(FALSE),
             * which changes the NC area and causes Windows to send a synchronous
             * WM_SIZE back to the target.  If we forwarded that re-entrant WM_SIZE
             * to origProc, the TreeView would call ShowScrollBar(TRUE) again
             * (content still overflows from its perspective) → NC changes again
             * → another WM_SIZE → infinite blink loop.
             * Solution: swallow the re-entrant WM_SIZE entirely (return 0).
             * The outer invocation handles HideNativeBar + UpdateVisibility after
             * origProc returns, which leaves the correct final state. */
            if ((ctxV && ctxV->inWmSize) || (ctxH && ctxH->inWmSize))
                return 0;
            if (ctxV) ctxV->inWmSize = TRUE;
            if (ctxH) ctxH->inWmSize = TRUE;
            LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            /* RichEdit text may reflow on size change (line wrapping changes
             * when client width changes); flush the cached document height so
             * Msb_ContentOverflows re-measures on the next call. */
            if (ctxV && ctxV->isRichEdit) ctxV->richVertMax = 0;
            /* For TreeView/ListView targets, ShowScrollBar(FALSE) inside
             * Msb_HideNativeBar can reset nMax to 0 even when content genuinely
             * overflows.  Capture the real SCROLLINFO BEFORE hiding; restore it
             * after so Msb_UpdateVisibilityGuarded and Msb_Layout see correct
             * values.  Only restore when the pre-hide range was non-zero
             * (i.e. content really did overflow before we hid the bar). */
            /* Suppress native bars.  For TreeView V-axis, reconstruct
             * SCROLLINFO from live tree state (item count + first-visible
             * index) instead of capture/restore: ShowScrollBar(FALSE) resets
             * nMax/nPos to 0, and the pre-hide capture is only valid if
             * origProc(WM_SIZE) actually recomputed SCROLLINFO (not guaranteed
             * for unchanged client size).  H-axis uses capture/restore because
             * nMax is set at item insertion time and reliably non-zero. */
            SCROLLINFO siPreH = {sizeof(siPreH)};
            BOOL restoreH = FALSE;
            if (ctxH && (ctxH->isTreeView || ctxH->isListView)) {
                siPreH.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
                GetScrollInfo(hwnd, SB_HORZ, &siPreH);
                restoreH = (siPreH.nMax > siPreH.nMin);
                /* Sync lvHPos from the header control on real layout changes
                 * (column resize, window resize).  Guard: skip when our bar
                 * has capture — WM_SIZE can fire mid-scroll due to the
                 * ShowScrollBar(TRUE/FALSE) client-area change, and we do not
                 * want to overwrite the lvHPos that Msb_ScrollToPos just set. */
                if (ctxH->isListView) {
                    HWND cap = GetCapture();
                    /* Also skip the sync when WM_SIZE fires from within a
                     * WM_NCPAINT handler (inNcPaint=TRUE).  LVM_SCROLL
                     * triggers WM_NCPAINT synchronously BEFORE the header
                     * repositions its items, so Msb_GetListViewHPos() would
                     * return the old/stale position and corrupt lvHPos.
                     * After WM_NCPAINT completes (inNcPaint cleared), the
                     * next genuine WM_SIZE (window resize) syncs correctly. */
                    BOOL inNcPaintChain = ctxH->inNcPaint ||
                                         (ctxV && ctxV->inNcPaint);
                    if (cap != ctxH->hBar && !inNcPaintChain)
                        ctxH->lvHPos = Msb_GetListViewHPos(hwnd);
                }
            }
            if (ctxV) Msb_HideNativeBar(hwnd, ctxV->isRichEdit, TRUE);
            if (ctxH) Msb_HideNativeBar(hwnd, ctxH->isRichEdit, FALSE);
            /* For TreeView V-axis: reconstruct SCROLLINFO from tree state. */
            if (ctxV && ctxV->isTreeView) Msb_ReconstructTreeVScrollInfo(hwnd);
            if (restoreH) SetScrollInfo(hwnd, SB_HORZ, &siPreH, FALSE);
            /* WM_SIZE reflects a real layout change (not a transient scroll event),
             * so use the UNGUARDED check: bars may correctly go INVISIBLE here when
             * content genuinely fits after a resize or after expand/collapse causes
             * a native-bar show/hide that sends WM_SIZE to the target. */
            if (ctxV) { Msb_UpdateVisibility(ctxV); Msb_PositionBar(ctxV); Msb_Layout(ctxV); InvalidateRect(ctxV->hBar, NULL, FALSE); }
            if (ctxH) { Msb_UpdateVisibility(ctxH); Msb_PositionBar(ctxH); Msb_Layout(ctxH); InvalidateRect(ctxH->hBar, NULL, FALSE); }
            /* Keep the RichEdit format rect inside the bar edges so the last
             * line is scrollable fully above the H bar. */
            if (any->isRichEdit) Msb_ApplyRichFormatRect(hwnd);
            if (ctxV) ctxV->inWmSize = FALSE;
            if (ctxH) ctxH->inWmSize = FALSE;
            return r;
        }

        case WM_VSCROLL: {
            LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            if (ctxV) {
                Msb_HideNativeBar(hwnd, ctxV->isRichEdit, TRUE);
                /* TreeView: reconstruct from tree state (capture/restore fails
                 * because ShowScrollBar(FALSE) zeroes nMax/nPos). */
                if (ctxV->isTreeView) Msb_ReconstructTreeVScrollInfo(hwnd);
                else if (ctxV->isListView) {
                    SCROLLINFO siPre = {}; siPre.cbSize = sizeof(siPre); siPre.fMask = SIF_ALL;
                    GetScrollInfo(hwnd, SB_VERT, &siPre);
                    if (siPre.nMax > siPre.nMin) SetScrollInfo(hwnd, SB_VERT, &siPre, FALSE);
                }
            }
            if (any->isRichEdit) Msb_ApplyRichFormatRect(hwnd);
            if (ctxV) {
                /* For TreeView/ListView, use the guarded variant that allows
                 * INVISIBLE→HIDDEN (bar appears when content first overflows)
                 * but blocks HIDDEN→INVISIBLE from a transient zero scroll-range. */
                Msb_UpdateVisibilityGuarded(ctxV);
                Msb_Layout(ctxV);
                InvalidateRect(ctxV->hBar, NULL, FALSE);
                UpdateWindow(ctxV->hBar);
            }
            return r;
        }

        case WM_HSCROLL: {
            LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            if (ctxH) {
                /* Same capture/restore for TreeView/ListView H-bar. */
                SCROLLINFO siPre = {}; BOOL restore = FALSE;
                if (ctxH->isTreeView || ctxH->isListView) {
                    siPre.cbSize = sizeof(siPre); siPre.fMask = SIF_ALL;
                    GetScrollInfo(hwnd, SB_HORZ, &siPre);
                    restore = (siPre.nMax > siPre.nMin);
                    /* Do NOT update lvHPos here for ListView: WM_HSCROLL may come
                     * from Msb_Scroll (which has already updated lvHPos via
                     * Msb_ScrollToPos) or from the tilt-wheel path.  The tilt-wheel
                     * path updates lvHPos separately after this interceptor returns.
                     * Trusting siPre.nPos is unsafe because ShowScrollBar(FALSE) may
                     * have zeroed it before origProc ran. */
                }
                Msb_HideNativeBar(hwnd, ctxH->isRichEdit, FALSE);
                if (restore) SetScrollInfo(hwnd, SB_HORZ, &siPre, FALSE);
                if (any->isRichEdit) Msb_ApplyRichFormatRect(hwnd);
                /* Clamp position: EM_SETTARGETDEVICE gives a 32767-px scroll range;
                 * enforce our measured content width so the user can't scroll into
                 * empty space beyond the actual text. */
                Msb_ClampRichHorzPos(ctxH);
                /* Same guarded approach: allows INVISIBLE→HIDDEN for LV/TV,
                 * blocks HIDDEN→INVISIBLE from transient zero-range events. */
                Msb_UpdateVisibilityGuarded(ctxH);
                Msb_Layout(ctxH);
                InvalidateRect(ctxH->hBar, NULL, FALSE);
                UpdateWindow(ctxH->hBar);
            }
            return r;
        }

        case WM_MOUSEWHEEL: {
            /* Own implementation — suppress RichEdit's native wheel handling
             * because it doesn't update SCROLLINFO.nPos reliably. */
            if (!ctxV) break;   /* no vertical bar — fall through to default */
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            UINT sysLines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &sysLines, 0);
            int steps; UINT cmd;
            if (sysLines == WHEEL_PAGESCROLL) {
                steps = 1;
                cmd = (delta > 0) ? SB_PAGEUP : SB_PAGEDOWN;
            } else {
                ctxV->wheelAccum += delta * (int)sysLines;
                steps = ctxV->wheelAccum / WHEEL_DELTA;
                ctxV->wheelAccum -= steps * WHEEL_DELTA;
                cmd   = (steps > 0) ? SB_LINEUP : SB_LINEDOWN;
                steps = abs(steps);
            }
            for (int i = 0; i < steps; i++)
                CallWindowProcW(any->origProc, hwnd, WM_VSCROLL,
                                MAKEWPARAM(cmd, 0), 0);
            /* For TreeView: reconstruct SCROLLINFO from tree state after hide.
             * Capture/restore fails because ShowScrollBar(FALSE) zeroes nMax. */
            Msb_HideNativeBar(hwnd, ctxV->isRichEdit, TRUE);
            if (ctxV->isTreeView) Msb_ReconstructTreeVScrollInfo(hwnd);
            /* Keep bar in hint-strip mode — only update position indicator.
             * Never expand toward full width on wheel scroll. */
            if (Msb_UpdateVisibilityGuarded(ctxV)) {
                ctxV->thumbState = THUMB_DRAG;  /* drag colour while wheel spins */
                Msb_Layout(ctxV); InvalidateRect(ctxV->hBar, NULL, FALSE); UpdateWindow(ctxV->hBar);
                /* Timer 2: reset thumb colour 220 ms after last wheel tick */
                SetTimer(ctxV->hBar, 2, 220, NULL);
            }
            return 0;   /* suppress original */
        }

        case WM_MOUSEHWHEEL: {
            if (!ctxH) break;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            UINT sysLines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS, 0, &sysLines, 0);
            ctxH->wheelAccum += delta * (int)sysLines;
            int steps = ctxH->wheelAccum / WHEEL_DELTA;
            ctxH->wheelAccum -= steps * WHEEL_DELTA;
            UINT cmd = (steps > 0) ? SB_LINERIGHT : SB_LINELEFT;
            steps = abs(steps);
            if (ctxH->isListView) {
                /* ListView H: use Msb_ScrollToPos (LVM_SCROLL + lvHPos tracking)
                 * for each step so position is always accurate — same as Msb_Scroll. */
                SCROLLINFO siW = {sizeof(siW), SIF_ALL};
                Msb_FixListViewHScrollInfo(hwnd, &siW);
                int maxPos = max(0, (int)(siW.nMax - (int)siW.nPage + 1));
                /* Small fixed pixel step per tilt-wheel notch line. */
                const int lineStep = S(ctxH, 20);
                int newPos = ctxH->lvHPos;
                for (int i = 0; i < steps; i++)
                    newPos += (cmd == SB_LINERIGHT) ? lineStep : -lineStep;
                if (newPos < 0)       newPos = 0;
                if (newPos > maxPos)  newPos = maxPos;
                Msb_ScrollToPos(ctxH, newPos);
            } else {
                /* Non-ListView: original WM_HSCROLL path. */
                for (int i = 0; i < steps; i++)
                    CallWindowProcW(any->origProc, hwnd, WM_HSCROLL,
                                    MAKEWPARAM(cmd, 0), 0);
            }
            /* Capture SCROLLINFO before HideNativeBar resets nMax. */
            SCROLLINFO siPreH = {}; BOOL restoreH = FALSE;
            if (ctxH->isTreeView || ctxH->isListView) {
                siPreH.cbSize = sizeof(siPreH); siPreH.fMask = SIF_ALL;
                GetScrollInfo(hwnd, SB_HORZ, &siPreH);
                restoreH = (siPreH.nMax > siPreH.nMin);
            }
            Msb_HideNativeBar(hwnd, ctxH->isRichEdit, FALSE);
            if (restoreH) SetScrollInfo(hwnd, SB_HORZ, &siPreH, FALSE);
            /* Clamp scroll position — same reason as in WM_HSCROLL. */
            Msb_ClampRichHorzPos(ctxH);
            /* Keep bar in hint-strip mode — only update position indicator. */
            if (Msb_UpdateVisibilityGuarded(ctxH)) {
                ctxH->thumbState = THUMB_DRAG;
                Msb_Layout(ctxH); InvalidateRect(ctxH->hBar, NULL, FALSE); UpdateWindow(ctxH->hBar);
                SetTimer(ctxH->hBar, 2, 220, NULL);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            /* Proximity expand: trigger fade-in when cursor reaches the bar's edge
             * zone — only when content overflows (state FADE_HIDDEN or FADE_INVISIBLE
             * after a previous fade-out).  FADE_INVISIBLE+overflow → show window first. */
            RECT rcT; GetClientRect(hwnd, &rcT);
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            if (ctxV && !(ctxV->flags & MSB_NOHIDE) &&
                ctxV->fadeState == FADE_INVISIBLE &&
                Msb_ContentOverflows(ctxV) &&
                mx >= rcT.right - S(ctxV, MSB_WIDTH_FULL)) {
                /* Only trigger from FADE_INVISIBLE: when bar is FADE_HIDDEN (3px
                 * hint strip showing), the bar's own WM_MOUSEMOVE handles expansion
                 * once the cursor enters the strip.  Expanding from the target's
                 * proximity zone while already FADE_HIDDEN causes an infinite loop:
                 * bar settles at 3px → target re-expands → WM_MOUSELEAVE contracts
                 * → bar reaches FADE_HIDDEN again → repeat. */
                if (ctxV->fadeState == FADE_INVISIBLE) {
                    ctxV->fadeWidth = 0.0f;
                    Msb_PositionBar(ctxV);
                    ShowWindow(ctxV->hBar, SW_SHOWNOACTIVATE);
                }
                KillTimer(ctxV->hBar, 3);
                ctxV->fadeState = FADE_EXPANDING;
                SetTimer(ctxV->hBar, 3, 16, NULL);
            }
            /* H-scroll Step 1: proximity-expand suppressed for H-bar. */
            break;
        }

        case LVM_DELETEALLITEMS: {
            /* When all ListView items are deleted the scroll position resets to 0.
             * Intercept here so callers do not need ShowScrollBar+msb_sync after
             * ListView_DeleteAllItems; the library keeps lvHPos in sync itself. */
            LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            if (ctxH && ctxH->isListView) {
                ctxH->lvHPos = 0;
                Msb_UpdateVisibility(ctxH);
                Msb_Layout(ctxH);
                InvalidateRect(ctxH->hBar, NULL, FALSE);
                UpdateWindow(ctxH->hBar);
            }
            return r;
        }

        case LVM_SETCOLUMNWIDTH: {
            /* Intercept programmatic column-width changes (AUTOSIZE / fixed px).
             * After origProc the ListView has committed the new width and the
             * header item rects are settled, so Msb_GetListViewHPos returns the
             * true horizontal scroll position.  Callers no longer need
             * ShowScrollBar+msb_sync after every ListView_SetColumnWidth call. */
            LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            if (ctxH && ctxH->isListView) {
                ctxH->lvHPos = Msb_GetListViewHPos(hwnd);
                Msb_UpdateVisibility(ctxH);
                Msb_Layout(ctxH);
                InvalidateRect(ctxH->hBar, NULL, FALSE);
                UpdateWindow(ctxH->hBar);
            }
            return r;
        }

        case WM_NOTIFY: {
            /* Intercept HDN_ENDTRACK from the ListView header so the library
             * handles column-resize sync internally.  After origProc the column
             * widths are committed and the header item rects are settled.
             * Callers no longer need a WM_NOTIFY/HDN_ENDTRACK handler to call
             * msb_reposition — the library does it automatically. */
            if (ctxH && ctxH->isListView) {
                NMHDR* nm = (NMHDR*)lParam;
                HWND hHdr = ListView_GetHeader(hwnd);
                if (hHdr && nm->hwndFrom == hHdr &&
                    (nm->code == HDN_ENDTRACKW || nm->code == HDN_ENDTRACKA)) {
                    LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
                    ctxH->lvHPos = Msb_GetListViewHPos(hwnd);
                    Msb_UpdateVisibility(ctxH);
                    Msb_Layout(ctxH);
                    InvalidateRect(ctxH->hBar, NULL, FALSE);
                    UpdateWindow(ctxH->hBar);
                    return r;
                }
            }
            break;
        }

        case WM_DESTROY:
            /* Target is being destroyed — clean up both bars, then chain through
             * to the original WndProc so its WM_DESTROY handler also runs.
             * origProc must be saved NOW: msb_detach frees the MsbCtx structs,
             * so ctxV/ctxH/any must not be touched after the first detach call. */
            {
                WNDPROC origProc = any->origProc;
                if (ctxV) msb_detach((HMSB)ctxV);
                if (ctxH) msb_detach((HMSB)ctxH);
                return CallWindowProcW(origProc, hwnd, msg, wParam, lParam);
            }
    }

    return CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
}

/* ── Bar sizing / positioning ───────────────────────────────────────────────*/

/*
 * Msb_PositionBar — size and place the bar window on the edge of
 * the target window's client area.  The bar is a child of the target's
 * parent (a sibling of the target), so it is never scrolled with the
 * target's content.  Coordinates are in the parent's client space.
 */
static void Msb_PositionBar(MsbCtx* ctx)
{
    /* Do not reposition while a bar window has mouse capture (drag, track-click,
     * arrow auto-repeat).  LVM_SCROLL internally calls ShowScrollBar(TRUE/FALSE)
     * mid-scroll, which fires WM_SIZE on the ListView before WM_NCPAINT.  That
     * WM_SIZE reaches here with a temporarily wrong client rect (native bar
     * briefly shown/hidden), making the bar jump a few px and back each step.
     * After ReleaseCapture the next normal layout call repositions correctly. */
    HWND captured = GetCapture();
    if (captured) {
        MsbCtx* pV = (MsbCtx*)GetPropW(ctx->hTarget, kMsbTargetPropV);
        MsbCtx* pH = (MsbCtx*)GetPropW(ctx->hTarget, kMsbTargetPropH);
        if ((pV && captured == pV->hBar) || (pH && captured == pH->hBar))
            return;
    }

    /* Use the actual parent of the bar window for coordinate mapping.
     * When the target is a top-level window (no parent), the bar was created
     * as a child of the target itself; GetParent(hBar) handles both cases. */
    HWND hBarParent = GetParent(ctx->hBar);

    /* Find the top-left of the target's client area in bar-parent coords. */
    POINT ptOrig = {0, 0};
    ClientToScreen(ctx->hTarget, &ptOrig);
    if (hBarParent)
        ScreenToClient(hBarParent, &ptOrig);
    /* else: hBarParent==NULL (desktop) — coords already in screen/desktop space */

    RECT rc;
    GetClientRect(ctx->hTarget, &rc);
    int cw = rc.right;
    int ch = rc.bottom;

    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
    int barW  = max(1, (int)(ctx->fadeWidth + 0.5f));

    if (vert) {
        /* Right edge, trimmed by insetNear (top) and insetFar (bottom). */
        /* Reserve exactly the space the H-bar currently occupies (fadeWidth),
         * plus its edgeGap and insetFar, so V-bar bottom is always flush with
         * H-bar top regardless of whether H is in hint-strip or full-width mode. */
        int hBarH = 0;
        MsbCtx* ctxH = (MsbCtx*)GetPropW(ctx->hTarget, kMsbTargetPropH);
        if (ctxH && IsWindow(ctxH->hBar) && ctxH->fadeState != FADE_INVISIBLE)
            hBarH = max((int)(ctxH->fadeWidth + 0.5f), S(ctxH, MSB_WIDTH_HIDDEN))
                    + ctxH->edgeGap + ctxH->insetFar;
        int top    = ctx->insetNear;
        int height = ch - ctx->insetNear - hBarH - ctx->insetFar;
        if (height < 1) height = 1;
        SetWindowPos(ctx->hBar, HWND_TOP,
                     ptOrig.x + cw - barW - ctx->edgeGap, ptOrig.y + top, barW, height,
                     SWP_NOACTIVATE);
    } else {
        /* Bottom edge, trimmed by insetFar (right) and insetNear (left); full width. */
        int top = ch - barW - ctx->insetFar - ctx->edgeGap;
        SetWindowPos(ctx->hBar, HWND_TOP,
                     ptOrig.x + ctx->insetNear, ptOrig.y + top,
                     cw - ctx->insetNear - ctx->insetFar, barW,
                     SWP_NOACTIVATE);
    }
}

/* ── Public API ─────────────────────────────────────────────────────────────*/

HMSB msb_attach(HWND hTarget, DWORD flags)
{
    if (!hTarget || !IsWindow(hTarget)) return NULL;

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hTarget, GWLP_HINSTANCE);
    if (!Msb_EnsureClass(hInst)) return NULL;

    MsbCtx* ctx = (MsbCtx*)calloc(1, sizeof(MsbCtx));
    if (!ctx) return NULL;

    ctx->hTarget    = hTarget;
    ctx->flags      = flags;
    ctx->isRichEdit = Msb_IsRichEdit(hTarget);
    ctx->isListView = Msb_IsListView(hTarget);
    ctx->isTreeView = Msb_IsTreeView(hTarget);
    ctx->dpiScale   = Msb_GetDpiScale(hTarget);

    /* Create the bar as a child of hTarget's parent (a sibling of hTarget),
     * so that scrolling hTarget's content does not move the bar window. */
    HWND hBarParent = GetParent(hTarget);
    if (!hBarParent) hBarParent = hTarget;   /* fallback: hTarget is top-level */
    ctx->hBar = CreateWindowExW(
        0, L"MyScrollbar", NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 1, 1,             /* positioned properly by Msb_PositionBar */
        hBarParent, NULL, hInst, NULL);

    if (!ctx->hBar) {
        free(ctx);
        return NULL;
    }

    /* Store context on the bar window */
    SetPropW(ctx->hBar, kMsbBarProp, (HANDLE)ctx);

    /* Subclass the target window — only install the subclass once.
     * If the other axis is already attached, reuse its origProc. */
    const wchar_t* myKey    = Msb_TargetPropKey(flags);
    const wchar_t* otherKey = Msb_OtherTargetPropKey(flags);
    MsbCtx* otherCtx = (MsbCtx*)GetPropW(hTarget, otherKey);
    if (otherCtx) {
        /* Already subclassed by the other axis — share its origProc. */
        ctx->origProc = otherCtx->origProc;
    } else {
        ctx->origProc = (WNDPROC)(LONG_PTR)
            SetWindowLongPtrW(hTarget, GWLP_WNDPROC,
                              (LONG_PTR)Msb_TargetSubclassProc);
    }
    SetPropW(hTarget, myKey, (HANDLE)ctx);

    /* Ensure the target does not paint over our bar window.
     * WS_CLIPSIBLINGS makes the target exclude sibling rects above it in
     * Z-order from its paint region, so the bar (HWND_TOP) is always visible. */
    LONG_PTR tStyle = GetWindowLongPtrW(hTarget, GWL_STYLE);
    if (!(tStyle & WS_CLIPSIBLINGS))
        SetWindowLongPtrW(hTarget, GWL_STYLE, tStyle | WS_CLIPSIBLINGS);

    /* Check overflow BEFORE hiding the native bar.
     * For TreeView targets, ShowScrollBar(FALSE) can cause the control to
     * reset its SB_HORZ SCROLLINFO range to 0, making the post-hide
     * Msb_ContentOverflows check return FALSE even when content genuinely
     * overflows.  Capture the state now so we can use it below. */
    BOOL preHideOverflows = FALSE;
    if (!(flags & MSB_NOHIDE)) {
        /* TreeView/ListView: nPage is 0 at attach time (control hasn't been
         * sized/painted yet), so (nMax - nMin - nPage) is always positive and
         * the overflow check gives a false positive.  Always start INVISIBLE
         * for these controls; the caller must send WM_SIZE after attach to
         * establish the real initial visibility state. */
        BOOL canTrustScrollInfo = !(ctx->isTreeView || ctx->isListView);
        if (canTrustScrollInfo) {
            BOOL vert2 = !(flags & MSB_HORIZONTAL);
            SCROLLINFO si2 = {}; si2.cbSize = sizeof(si2); si2.fMask = SIF_ALL;
            GetScrollInfo(hTarget, vert2 ? SB_VERT : SB_HORZ, &si2);
            preHideOverflows = ((si2.nMax - si2.nMin) - (int)si2.nPage > 0);
        }
    }

    /* Hide the native scrollbar (keep the WS_VSCROLL style so SCROLLINFO
     * is maintained by the target window — we just suppress the drawing). */
    BOOL vert = !(flags & MSB_HORIZONTAL);
    /* Capture the full SCROLLINFO before hiding so we can restore it below.
     * ShowScrollBar(FALSE) resets nMax to 0 for TreeView/ListView; restoring
     * gives Msb_Layout the correct nPage/nMax for the initial thumb size. */
    SCROLLINFO siPreAttach = {};
    siPreAttach.cbSize = sizeof(siPreAttach);
    siPreAttach.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    GetScrollInfo(hTarget, vert ? SB_VERT : SB_HORZ, &siPreAttach);
    Msb_HideNativeBar(hTarget, ctx->isRichEdit, vert);
    /* Restore SCROLLINFO if HideNativeBar reset it (TreeView/ListView quirk). */
    if ((ctx->isTreeView || ctx->isListView) && siPreAttach.nMax > siPreAttach.nMin)
        SetScrollInfo(hTarget, vert ? SB_VERT : SB_HORZ, &siPreAttach, FALSE);

    /* Initialise lvHPos for ListView H-axis from the pre-hide SCROLLINFO.
     * calloc zeroes it, but if the ListView was already scrolled when msb_attach
     * is called (its horizontal position survives item deletion/re-insertion
     * because column widths are unchanged), lvHPos must start at the real
     * scroll position or every subsequent LVM_SCROLL delta will be wrong. */
    if (ctx->isListView && !vert)
        ctx->lvHPos = siPreAttach.nPos;

    /* Initialise animation width: NOHIDE starts fully visible; auto-hide
     * starts invisible (bar window hidden) until content overflows. */
    if (flags & MSB_NOHIDE) {
        ctx->fadeState = FADE_VISIBLE;
        ctx->fadeWidth = (float)S(ctx, MSB_WIDTH_FULL);
    } else {
        ctx->fadeState = FADE_INVISIBLE;
        ctx->fadeWidth = 0.0f;
        ShowWindow(ctx->hBar, SW_HIDE);
        /* If content was already overflowing before we hid the native bar,
         * start in FADE_HIDDEN (hint strip visible) rather than FADE_INVISIBLE.
         * This matters for TreeView where ShowScrollBar(FALSE) resets nMax. */
        if (preHideOverflows) {
            ctx->fadeState = FADE_HIDDEN;
            ctx->fadeWidth = (float)S(ctx, MSB_WIDTH_HIDDEN);
            Msb_PositionBar(ctx);
            ShowWindow(ctx->hBar, SW_SHOWNOACTIVATE);
        }
    }

    /* If the peer bar already exists (e.g. H bar attaching after V bar was
     * already registered), tell it to re-position now that our fadeWidth is
     * set and our property is registered.  Without this, the V bar is stuck
     * at full height because it positioned itself before H was registered and
     * no WM_SIZE fires between the two msb_attach calls. */
    {
        const wchar_t* otherKey = Msb_OtherTargetPropKey(flags);
        MsbCtx* peerCtx = (MsbCtx*)GetPropW(hTarget, otherKey);
        if (peerCtx && IsWindow(peerCtx->hBar)) {
            Msb_PositionBar(peerCtx);
            Msb_Layout(peerCtx);
            InvalidateRect(peerCtx->hBar, NULL, FALSE);
            UpdateWindow(peerCtx->hBar);
        }
    }

    /* Position and do initial layout; then let Msb_UpdateVisibility decide
     * whether to make the bar visible (content may not yet overflow). */
    Msb_PositionBar(ctx);
    Msb_Layout(ctx);
    if (!(flags & MSB_NOHIDE))
        Msb_UpdateVisibility(ctx);   /* may switch to FADE_HIDDEN + SW_SHOWNOACTIVATE */
    else {
        InvalidateRect(ctx->hBar, NULL, FALSE);
        UpdateWindow(ctx->hBar);
    }

    /* After Msb_UpdateVisibility the fade state may have changed from INVISIBLE
     * to HIDDEN (hidden-mode bars start invisible and transition immediately if
     * content overflows).  Re-notify the peer bar so it can recompute its size
     * now that our fadeState is accurate — e.g. the V bar must reserve space for
     * the H bar at the bottom, but during the initial attach the H bar was still
     * INVISIBLE when the peer notification ran, causing the V bar to use full height. */
    {
        const wchar_t* peerKey2 = Msb_OtherTargetPropKey(flags);
        MsbCtx* peer2 = (MsbCtx*)GetPropW(hTarget, peerKey2);
        if (peer2 && IsWindow(peer2->hBar)) {
            Msb_PositionBar(peer2);
            Msb_Layout(peer2);
            InvalidateRect(peer2->hBar, NULL, FALSE);
            UpdateWindow(peer2->hBar);
        }
    }

    /* For RichEdit targets, shrink the formatting rectangle so the last line
     * is scrollable fully above the H bar (called on every attach so the
     * second call — when H bar attaches after V — has both contexts set). */
    if (ctx->isRichEdit)
        Msb_ApplyRichFormatRect(hTarget);

    return (HMSB)ctx;
}

void msb_detach(HMSB h)
{
    if (!h) return;
    MsbCtx* ctx = (MsbCtx*)h;

    if (ctx->hTarget && IsWindow(ctx->hTarget)) {
        const wchar_t* myKey    = Msb_TargetPropKey(ctx->flags);
        const wchar_t* otherKey = Msb_OtherTargetPropKey(ctx->flags);
        RemovePropW(ctx->hTarget, myKey);
        /* Only restore the original proc when the last bar is detaching. */
        if (!GetPropW(ctx->hTarget, otherKey)) {
            SetWindowLongPtrW(ctx->hTarget, GWLP_WNDPROC, (LONG_PTR)ctx->origProc);
        }
        /* Restore native scrollbar for this axis */
        BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
        Msb_ShowNativeBar(ctx->hTarget, ctx->isRichEdit, vert);
    }

    if (ctx->hBar && IsWindow(ctx->hBar)) {
        RemovePropW(ctx->hBar, kMsbBarProp);
        DestroyWindow(ctx->hBar);
    }

    free(ctx);
}

void msb_sync(HMSB h)
{
    if (!h) return;
    MsbCtx* ctx = (MsbCtx*)h;
    if (!ctx->hBar || !IsWindow(ctx->hBar)) return;
    /* For ListView H-axis: the caller is expected to have done capture/restore
     * of SCROLLINFO before calling msb_sync (e.g. the suppress pattern in
     * deps.cpp).  That makes GetScrollInfo.nPos reliable at this point.
     * Sync lvHPos from it so that external scroll resets (ListView_DeleteAllItems
     * resets the scroll position to 0) are reflected in our tracked position. */
    if (ctx->isListView && (ctx->flags & MSB_HORIZONTAL)) {
        /* Use the header control rect — GetScrollInfo.nPos is unreliable
         * (zeroed by ShowScrollBar(FALSE); not updated by LVM_SCROLL). */
        ctx->lvHPos = Msb_GetListViewHPos(ctx->hTarget);
    }
    /* Re-evaluate overflow: may show or hide the bar window. */
    if (!(ctx->flags & MSB_NOHIDE)) {
        if (!Msb_UpdateVisibility(ctx)) return;  /* bar hidden — nothing to paint */
    }
    Msb_Layout(ctx);
    InvalidateRect(ctx->hBar, NULL, FALSE);
    UpdateWindow(ctx->hBar);
}

void msb_reposition(HMSB h)
{
    if (!h) return;
    MsbCtx* ctx = (MsbCtx*)h;
    if (!ctx->hBar || !IsWindow(ctx->hBar)) return;
    if (!ctx->hTarget || !IsWindow(ctx->hTarget)) return;
    /* Suppress native bar — TreeView may have re-enabled it since the last
     * subclass-proc handler ran (e.g. TVN_ITEMEXPANDED causes the control to
     * call ShowScrollBar(TRUE) internally when content overflows). */
    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
    /* For ListView/TreeView H-axis: capture SCROLLINFO before hiding because
     * ShowScrollBar(SB_HORZ, FALSE) resets nMax/nPage/nPos to 0, causing
     * Msb_ContentOverflows to return FALSE and hiding the bar after every
     * WM_SIZE repositioning even when content genuinely overflows. */
    SCROLLINFO siPreH = {sizeof(siPreH)};
    BOOL restoreH = FALSE;
    if (!vert && (ctx->isListView || ctx->isTreeView)) {
        siPreH.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        GetScrollInfo(ctx->hTarget, SB_HORZ, &siPreH);
        restoreH = (siPreH.nMax > siPreH.nMin);
        /* Sync lvHPos from header — native bar is still active here, so the
         * header item rect gives the true visual offset (e.g. after a column
         * resize that shifted the scroll position). */
        if (ctx->isListView)
            ctx->lvHPos = Msb_GetListViewHPos(ctx->hTarget);
    }
    Msb_HideNativeBar(ctx->hTarget, ctx->isRichEdit, vert);
    /* For TreeView V-axis: reconstruct SCROLLINFO from tree state because
     * ShowScrollBar(FALSE) zeroed nMax/nPos. */
    if (ctx->isTreeView && vert)
        Msb_ReconstructTreeVScrollInfo(ctx->hTarget);
    if (restoreH)
        SetScrollInfo(ctx->hTarget, SB_HORZ, &siPreH, FALSE);
    /* Now check overflow and, if visible, reposition and relayout. */
    if (!Msb_UpdateVisibility(ctx)) return;  /* bar hidden — nothing to do */
    /* Re-position from the target's current client rect corners. */
    Msb_PositionBar(ctx);
    /* Recompute thumb from fresh SCROLLINFO. */
    Msb_Layout(ctx);
    InvalidateRect(ctx->hBar, NULL, FALSE);
    UpdateWindow(ctx->hBar);
}

void msb_notify_content_changed(HMSB h)
{
    if (!h) return;
    MsbCtx* ctx = (MsbCtx*)h;
    /* Invalidate cached content measurements so Msb_Layout re-measures. */
    ctx->richHorzMax = 0;
    ctx->richVertMax = 0;
    /* Also clear the peer bar's caches — content changes affect both axes. */
    const wchar_t* otherKey = Msb_OtherTargetPropKey(ctx->flags);
    MsbCtx* peer = (MsbCtx*)GetPropW(ctx->hTarget, otherKey);
    if (peer) {
        peer->richHorzMax = 0;
        peer->richVertMax = 0;
        msb_sync((HMSB)peer);
    }
    msb_sync(h);
}

void msb_set_insets(HMSB h, int insetNear, int insetFar)
{
    if (!h) return;
    MsbCtx* ctx = (MsbCtx*)h;
    ctx->insetNear = insetNear;
    ctx->insetFar  = insetFar;
    Msb_PositionBar(ctx);
    Msb_Layout(ctx);
    InvalidateRect(ctx->hBar, NULL, FALSE);
    UpdateWindow(ctx->hBar);
}

HWND msb_get_bar_hwnd(HMSB h)
{
    if (!h) return NULL;
    return ((MsbCtx*)h)->hBar;
}

void msb_set_edge_gap(HMSB h, int gap)
{
    if (!h) return;
    MsbCtx* ctx = (MsbCtx*)h;
    ctx->edgeGap = gap;
    /* Suppress native bar first so GetClientRect in Msb_PositionBar sees the
     * full client dimensions rather than a native-scrollbar-reduced rect. */
    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
    Msb_HideNativeBar(ctx->hTarget, ctx->isRichEdit, vert);
    Msb_PositionBar(ctx);
    Msb_Layout(ctx);
    InvalidateRect(ctx->hBar, NULL, FALSE);
    UpdateWindow(ctx->hBar);
}

