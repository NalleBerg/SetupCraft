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

/* ── Internal context ───────────────────────────────────────────────────────*/

typedef struct MsbCtx {
    /* Ownership */
    HWND    hBar;           /* the custom scrollbar child window              */
    HWND    hTarget;        /* the window being scrolled                      */
    WNDPROC origProc;       /* saved target window proc (restored on detach)  */
    DWORD   flags;          /* MSB_* flags passed to msb_attach               */
    BOOL    isRichEdit;     /* TRUE if hTarget is a RichEdit                  */

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

    int trackLen = vert ? (ctx->rTrack.bottom - ctx->rTrack.top)
                        : (ctx->rTrack.right  - ctx->rTrack.left);
    int thumbMin = S(ctx, MSB_THUMB_MIN);

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
    HBRUSH hBgBr = CreateSolidBrush(MSB_CLR_TRACK);
    FillRect(hdc, &rc, hBgBr);
    DeleteObject(hBgBr);

    /* ── Arrow up / left ─────────────────────────────────────────────────── */
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

    /* ── Thumb ───────────────────────────────────────────────────────────── */
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
    SendMessageW(ctx->hTarget, msg, MAKEWPARAM(cmd, 0), 0);
    /* re-read SCROLLINFO and repaint */
    Msb_Layout(ctx);
    InvalidateRect(ctx->hBar, NULL, FALSE);
    UpdateWindow(ctx->hBar);
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
                SCROLLINFO si    = {sizeof(si), SIF_POS};
                GetScrollInfo(ctx->hTarget,
                              vert ? SB_VERT : SB_HORZ, &si);
                ctx->dragStartPos = si.nPos;
                ctx->dragCurPos   = si.nPos;
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                /* Arrow / page click */
                UINT cmd = (hit == 1) ? (UINT)(!(ctx->flags & MSB_HORIZONTAL) ? SB_LINEUP  : SB_LINELEFT)
                         : (hit == 2) ? (UINT)(!(ctx->flags & MSB_HORIZONTAL) ? SB_LINEDOWN : SB_LINERIGHT)
                         : (hit == 3) ? (UINT)SB_PAGEUP
                         :              (UINT)SB_PAGEDOWN;
                ctx->repeatCmd  = cmd;
                ctx->timerFirst = TRUE;
                Msb_Scroll(ctx, cmd);
                /* Initial delay before auto-repeat */
                ctx->timerId = SetTimer(hwnd, 1, 350, NULL);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!ctx || !ctx->dragging) break;
            BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
            int cur   = vert ? GET_Y_LPARAM(lParam) : GET_X_LPARAM(lParam);
            int delta = cur - ctx->dragStartPx;

            /* Map pixel delta to scroll units */
            SCROLLINFO si = {sizeof(si), SIF_ALL};
            GetScrollInfo(ctx->hTarget, vert ? SB_VERT : SB_HORZ, &si);
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

            UINT smsg = vert ? WM_VSCROLL : WM_HSCROLL;
            SendMessageW(ctx->hTarget, smsg,
                         MAKEWPARAM(SB_THUMBTRACK, (WORD)newPos), 0);
            Msb_Layout(ctx);
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateWindow(hwnd);
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!ctx) break;
            if (ctx->timerId) {
                KillTimer(hwnd, ctx->timerId);
                ctx->timerId = 0;
            }
            if (ctx->dragging) {
                ctx->dragging   = FALSE;
                ctx->thumbState = THUMB_NORMAL;
                BOOL vert       = !(ctx->flags & MSB_HORIZONTAL);
                UINT smsg       = vert ? WM_VSCROLL : WM_HSCROLL;
                SendMessageW(ctx->hTarget, smsg,
                             MAKEWPARAM(SB_THUMBPOSITION, (WORD)ctx->dragCurPos), 0);
                SendMessageW(ctx->hTarget, smsg, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
                Msb_Layout(ctx);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            ReleaseCapture();
            return 0;
        }

        case WM_TIMER: {
            if (!ctx || wParam != 1) break;
            if (ctx->timerFirst) {
                /* Switch to fast repeat rate */
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
            if (ctx && ctx->dragging) {
                ctx->dragging   = FALSE;
                ctx->thumbState = THUMB_NORMAL;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
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
            if (ctxV) { Msb_PositionBar(ctxV); Msb_Layout(ctxV); InvalidateRect(ctxV->hBar, NULL, FALSE); }
            if (ctxH) { Msb_PositionBar(ctxH); Msb_Layout(ctxH); InvalidateRect(ctxH->hBar, NULL, FALSE); }
            /* RichEdit may re-show native bars after resize — suppress again. */
            if (ctxV) Msb_HideNativeBar(hwnd, ctxV->isRichEdit, TRUE);
            if (ctxH) Msb_HideNativeBar(hwnd, ctxH->isRichEdit, FALSE);
            return r;
        }

        case WM_VSCROLL: {
            LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            if (ctxV) Msb_HideNativeBar(hwnd, ctxV->isRichEdit, TRUE);
            if (ctxV) { Msb_Layout(ctxV); InvalidateRect(ctxV->hBar, NULL, FALSE); UpdateWindow(ctxV->hBar); }
            return r;
        }

        case WM_HSCROLL: {
            LRESULT r = CallWindowProcW(any->origProc, hwnd, msg, wParam, lParam);
            if (ctxH) Msb_HideNativeBar(hwnd, ctxH->isRichEdit, FALSE);
            if (ctxH) { Msb_Layout(ctxH); InvalidateRect(ctxH->hBar, NULL, FALSE); UpdateWindow(ctxH->hBar); }
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
            Msb_Layout(ctxV); InvalidateRect(ctxV->hBar, NULL, FALSE); UpdateWindow(ctxV->hBar);
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
            UINT cmd = (steps > 0) ? SB_LINELEFT : SB_LINERIGHT;
            steps = abs(steps);
            for (int i = 0; i < steps; i++)
                CallWindowProcW(any->origProc, hwnd, WM_HSCROLL,
                                MAKEWPARAM(cmd, 0), 0);
            Msb_HideNativeBar(hwnd, ctxH->isRichEdit, FALSE);
            Msb_Layout(ctxH); InvalidateRect(ctxH->hBar, NULL, FALSE); UpdateWindow(ctxH->hBar);
            return 0;
        }

        case WM_DESTROY:
            /* Target is being destroyed — clean up both bars. */
            if (ctxV) msb_detach((HMSB)ctxV);
            if (ctxH) msb_detach((HMSB)ctxH);
            return 0;
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
    HWND hParent = GetParent(ctx->hTarget);

    /* Find the top-left of the target's client area in parent coords. */
    POINT ptOrig = {0, 0};
    ClientToScreen(ctx->hTarget, &ptOrig);
    ScreenToClient(hParent, &ptOrig);

    RECT rc;
    GetClientRect(ctx->hTarget, &rc);
    int cw = rc.right;
    int ch = rc.bottom;

    BOOL vert = !(ctx->flags & MSB_HORIZONTAL);
    int barW  = S(ctx, MSB_WIDTH_FULL);

    if (vert) {
        /* Right edge, full height */
        SetWindowPos(ctx->hBar, HWND_TOP,
                     ptOrig.x + cw - barW, ptOrig.y, barW, ch,
                     SWP_NOACTIVATE);
    } else {
        /* Bottom edge, full width */
        SetWindowPos(ctx->hBar, HWND_TOP,
                     ptOrig.x, ptOrig.y + ch - barW, cw, barW,
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

    /* Hide the native scrollbar (keep the WS_VSCROLL style so SCROLLINFO
     * is maintained by the target window — we just suppress the drawing). */
    BOOL vert = !(flags & MSB_HORIZONTAL);
    Msb_HideNativeBar(hTarget, ctx->isRichEdit, vert);

    /* Position and do initial layout */
    Msb_PositionBar(ctx);
    Msb_Layout(ctx);

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
    Msb_Layout(ctx);
    InvalidateRect(ctx->hBar, NULL, FALSE);
    UpdateWindow(ctx->hBar);
}
