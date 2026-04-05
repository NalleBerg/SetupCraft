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

#include "my_scrollbar.h"
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

    /* Measure line height as the Y difference between line 0 and line 1. */
    int lineH = 0;
    if (lineCount >= 2) {
        int c0 = (int)SendMessageW(hRE, EM_LINEINDEX, 0, 0);
        int c1 = (int)SendMessageW(hRE, EM_LINEINDEX, 1, 0);
        if (c0 >= 0 && c1 > c0) {
            POINTL pt0 = {}, pt1 = {};
            SendMessageW(hRE, EM_POSFROMCHAR, (WPARAM)&pt0, (LPARAM)c0);
            SendMessageW(hRE, EM_POSFROMCHAR, (WPARAM)&pt1, (LPARAM)c1);
            lineH = (int)(pt1.y - pt0.y);
        }
    }
    if (lineH <= 0) {
        /* Single line or measurement failed — fall back to text metrics. */
        HDC hdc = GetDC(hRE);
        if (hdc) {
            TEXTMETRICW tm = {};
            GetTextMetricsW(hdc, &tm);
            ReleaseDC(hRE, hdc);
            lineH = tm.tmHeight + tm.tmExternalLeading;
        }
    }
    if (lineH <= 0) lineH = 16; /* ultimate fallback */

    /* Top of last line in client coords + vertical scroll offset = document Y.
     * Add half a line of breathing room so the last line isn't flush against
     * the bottom of the scrollable area. */
    int lastLine = lineCount - 1;
    int lastChar = (int)SendMessageW(hRE, EM_LINEINDEX, lastLine, 0);
    POINTL ptLast = {};
    SendMessageW(hRE, EM_POSFROMCHAR, (WPARAM)&ptLast, (LPARAM)lastChar);
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
    int scrollRange = (si.nMax - si.nMin) - (int)si.nPage;
    return (scrollRange > 0);
}

/* Show or hide the bar window depending on whether content overflows.
 * In NOHIDE mode the bar is always shown.  Returns TRUE if visible. */
static BOOL Msb_UpdateVisibility(MsbCtx* ctx)
{
    if (ctx->flags & MSB_NOHIDE) return TRUE;
    BOOL overflows = Msb_ContentOverflows(ctx);
    if (!overflows) {
        if (ctx->fadeState != FADE_INVISIBLE) {
            KillTimer(ctx->hBar, 3);
            ctx->fadeState = FADE_INVISIBLE;
            ctx->fadeWidth = 0.0f;
            ShowWindow(ctx->hBar, SW_HIDE);
        }
        return FALSE;
    }
    /* Content overflows — make sure the bar is visible at least as thin strip */
    if (ctx->fadeState == FADE_INVISIBLE) {
        ctx->fadeState = FADE_HIDDEN;
        ctx->fadeWidth = (float)S(ctx, MSB_WIDTH_HIDDEN);
        Msb_PositionBar(ctx);
        ShowWindow(ctx->hBar, SW_SHOWNOACTIVATE);
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

/* Send a scroll message to the target and re-sync the bar. */
static void Msb_Scroll(MsbCtx* ctx, UINT cmd)
{
    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
    UINT msg  = vert ? WM_VSCROLL : WM_HSCROLL;

    if (ctx->isListView) {
        /* ListView ignores external WM_VSCROLL/WM_HSCROLL for position changes.
         * Use LVM_SCROLL with a pixel delta instead. */
        int lineH = SendMessageW(ctx->hTarget, LVM_GETITEMSPACING, TRUE, 0);
        lineH = HIWORD(lineH);   /* icon-view item height; for report use row height */
        /* For report-mode ListView, item height = row height */
        RECT rcItem = {}; ListView_GetItemRect(ctx->hTarget, 0, &rcItem, LVIR_BOUNDS);
        int rowH = (rcItem.bottom > rcItem.top) ? (rcItem.bottom - rcItem.top) : S(ctx, 16);
        int dx = 0, dy = 0;
        switch (cmd) {
            case SB_LINEUP:   vert ? (dy = -rowH) : (dx = -S(ctx, 20)); break;
            case SB_LINEDOWN: vert ? (dy =  rowH) : (dx =  S(ctx, 20)); break;
            case SB_PAGEUP: {
                RECT rc; GetClientRect(ctx->hTarget, &rc);
                vert ? (dy = -(rc.bottom)) : (dx = -(rc.right)); break;
            }
            case SB_PAGEDOWN: {
                RECT rc; GetClientRect(ctx->hTarget, &rc);
                vert ? (dy =  rc.bottom) : (dx =  rc.right); break;
            }
        }
        SendMessageW(ctx->hTarget, LVM_SCROLL, (WPARAM)dx, (LPARAM)dy);
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
        SCROLLINFO si = {sizeof(si), SIF_POS};
        GetScrollInfo(ctx->hTarget, vert ? SB_VERT : SB_HORZ, &si);
        int delta = newPos - si.nPos;
        int dx, dy;
        if (vert) {
            /* Vertical SCROLLINFO.nPos is in rows; LVM_SCROLL expects pixels. */
            RECT rcItem = {};
            ListView_GetItemRect(ctx->hTarget, 0, &rcItem, LVIR_BOUNDS);
            int rowH = (rcItem.bottom > rcItem.top) ? (rcItem.bottom - rcItem.top) : S(ctx, 16);
            dx = 0;
            dy = delta * rowH;
        } else {
            /* Horizontal SCROLLINFO.nPos is already in pixels. */
            dx = delta;
            dy = 0;
        }
        if (dx || dy) SendMessageW(ctx->hTarget, LVM_SCROLL, (WPARAM)dx, (LPARAM)dy);
    } else {
        /* TreeView and other controls: SetScrollInfo + WM_VSCROLL/HSCROLL. */
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
                /* Use EM_GETSCROLLPOS for RichEdit — GetScrollInfo nPos is stale
                 * when the native bar is hidden. */
                int startPos = 0;
                if (ctx->isRichEdit) {
                    POINT pt = {0, 0};
                    SendMessageW(ctx->hTarget, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
                    startPos = vert ? pt.y : pt.x;
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
            /* Expand on first cursor entry when bar is in hidden-strip state.
             * FADE_INVISIBLE means content fits — bar shouldn't appear at all. */
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
                }
                int trackPx = vert ? (ctx->rTrack.bottom - ctx->rTrack.top)
                                   : (ctx->rTrack.right  - ctx->rTrack.left);
                int thumbPx = vert ? (ctx->rThumb.bottom - ctx->rThumb.top)
                                   : (ctx->rThumb.right  - ctx->rThumb.left);
                int range   = si.nMax - si.nMin - si.nPage + 1;
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
            ctx->thumbState     = THUMB_NORMAL;
            ctx->arrowUpState   = ARROW_NORMAL;
            ctx->arrowDownState = ARROW_NORMAL;
            InvalidateRect(hwnd, NULL, FALSE);
            /* Fade contract on mouse leave (hidden mode) */
            if (!(ctx->flags & MSB_NOHIDE) &&
                (ctx->fadeState == FADE_VISIBLE || ctx->fadeState == FADE_EXPANDING)) {
                KillTimer(hwnd, 3);
                ctx->fadeState = FADE_CONTRACTING;
                SetTimer(hwnd, 3, 16, NULL);
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
                float step    = (fullW - hiddenW) / 9.0f;  /* ~150 ms at 16 ms/tick */
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
        case WM_SIZE: {
            LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            /* Suppress native bars FIRST: the target may re-show them during its
             * own WM_SIZE (TreeView/ListView re-enable native bars when content
             * overflows).  Hiding them now guarantees GetClientRect returns the
             * full client rect — not reduced by a native scrollbar gutter —
             * before Msb_PositionBar reads it. */
            if (ctxV) Msb_HideNativeBar(hwnd, ctxV->isRichEdit, TRUE);
            if (ctxH) Msb_HideNativeBar(hwnd, ctxH->isRichEdit, FALSE);
            if (ctxV) { Msb_UpdateVisibilityGuarded(ctxV); Msb_PositionBar(ctxV); Msb_Layout(ctxV); InvalidateRect(ctxV->hBar, NULL, FALSE); }
            if (ctxH) { Msb_UpdateVisibilityGuarded(ctxH); Msb_PositionBar(ctxH); Msb_Layout(ctxH); InvalidateRect(ctxH->hBar, NULL, FALSE); }
            /* Keep the RichEdit format rect inside the bar edges so the last
             * line is scrollable fully above the H bar. */
            if (any->isRichEdit) Msb_ApplyRichFormatRect(hwnd);
            return r;
        }

        case WM_VSCROLL: {
            LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            if (ctxV) Msb_HideNativeBar(hwnd, ctxV->isRichEdit, TRUE);
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
                Msb_HideNativeBar(hwnd, ctxH->isRichEdit, FALSE);
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
            Msb_HideNativeBar(hwnd, ctxV->isRichEdit, TRUE);
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
            for (int i = 0; i < steps; i++)
                CallWindowProcW(any->origProc, hwnd, WM_HSCROLL,
                                MAKEWPARAM(cmd, 0), 0);
            Msb_HideNativeBar(hwnd, ctxH->isRichEdit, FALSE);
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
                (ctxV->fadeState == FADE_HIDDEN || ctxV->fadeState == FADE_INVISIBLE) &&
                Msb_ContentOverflows(ctxV) &&
                mx >= rcT.right - S(ctxV, MSB_WIDTH_FULL)) {
                if (ctxV->fadeState == FADE_INVISIBLE) {
                    ctxV->fadeWidth = 0.0f;
                    Msb_PositionBar(ctxV);
                    ShowWindow(ctxV->hBar, SW_SHOWNOACTIVATE);
                }
                KillTimer(ctxV->hBar, 3);
                ctxV->fadeState = FADE_EXPANDING;
                SetTimer(ctxV->hBar, 3, 16, NULL);
            }
            if (ctxH && !(ctxH->flags & MSB_NOHIDE) &&
                (ctxH->fadeState == FADE_HIDDEN || ctxH->fadeState == FADE_INVISIBLE) &&
                Msb_ContentOverflows(ctxH) &&
                my >= rcT.bottom - S(ctxH, MSB_WIDTH_FULL)) {
                if (ctxH->fadeState == FADE_INVISIBLE) {
                    ctxH->fadeWidth = 0.0f;
                    Msb_PositionBar(ctxH);
                    ShowWindow(ctxH->hBar, SW_SHOWNOACTIVATE);
                }
                KillTimer(ctxH->hBar, 3);
                ctxH->fadeState = FADE_EXPANDING;
                SetTimer(ctxH->hBar, 3, 16, NULL);
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
        int hBarH = 0;
        MsbCtx* ctxH = (MsbCtx*)GetPropW(ctx->hTarget, kMsbTargetPropH);
        if (ctxH && IsWindow(ctxH->hBar) && ctxH->fadeState != FADE_INVISIBLE)
            hBarH = S(ctxH, MSB_WIDTH_FULL);
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

    /* Hide the native scrollbar (keep the WS_VSCROLL style so SCROLLINFO
     * is maintained by the target window — we just suppress the drawing). */
    BOOL vert = !(flags & MSB_HORIZONTAL);
    Msb_HideNativeBar(hTarget, ctx->isRichEdit, vert);

    /* Initialise animation width: NOHIDE starts fully visible; auto-hide
     * starts invisible (bar window hidden) until content overflows. */
    if (flags & MSB_NOHIDE) {
        ctx->fadeState = FADE_VISIBLE;
        ctx->fadeWidth = (float)S(ctx, MSB_WIDTH_FULL);
    } else {
        ctx->fadeState = FADE_INVISIBLE;
        ctx->fadeWidth = 0.0f;
        ShowWindow(ctx->hBar, SW_HIDE);
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
    /* This is called from an explicit resize (WM_SIZE), not from a transient
     * scroll event, so use the unguarded visibility check.  If content
     * genuinely fits after the resize, hide the bar. */
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
    Msb_PositionBar(ctx);
    Msb_Layout(ctx);
    InvalidateRect(ctx->hBar, NULL, FALSE);
    UpdateWindow(ctx->hBar);
}

