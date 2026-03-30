#pragma once
/*
 * scrollbar.h — Reusable RichEdit scrollbar manager.
 *
 * STATUS: *** BELIEVED FIXED — awaiting test confirmation ***
 *   Thumb drag  (SB_THUMBTRACK / SB_THUMBPOSITION)   Works.
 *   Arrow/page clicks (SB_LINE*, SB_PAGE*)            Should work (native pass-through).
 *   Mouse wheel (WM_MOUSEWHEEL)                       Should work (forwarded as WM_MOUSEWHEEL).
 *
 * Root cause identified:
 *   Our earlier sync code (GetScrollInfo → SetScrollPos after CallWindowProcW)
 *   was overwriting the thumb position the RichEdit had just set correctly.
 *   The RTF editor works because its subclass does NO scroll handling at all.
 *   Fix: remove extra sync; for mousewheel, forward WM_MOUSEWHEEL to the
 *   RichEdit directly instead of calling EM_SCROLL externally.
 *
 * See scrollbar_API.txt for full analysis.
 *
 * ── Dependencies ────────────────────────────────────────────────────────────
 *   Pure Win32 — no MFC, no ATL, no SetupCraft helpers.
 *   Requires:  windows.h
 *   Link with: user32   (nothing extra)
 *
 * ── Lifecycle ────────────────────────────────────────────────────────────────
 *   HRESC h = RESC_Attach(hRichEdit);   // after WS_VSCROLL is set
 *   ...
 *   // in parent WM_MOUSEWHEEL:
 *   if (h) { RESC_OnMouseWheel(h, wParam); return 0; }
 *   ...
 *   // before DestroyWindow (or in parent WM_DESTROY):
 *   RESC_Detach(h);
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle returned by RESC_Attach. */
typedef struct RichEditScrollbarCtx* HRESC;

/*
 * RESC_Attach
 *   Subclasses hRichEdit so its WM_VSCROLL messages are intercepted.
 *   Call after the RichEdit has been created and WS_VSCROLL is in its style.
 *   Returns NULL on failure (bad hwnd, SetWindowLongPtrW error).
 */
HRESC  RESC_Attach(HWND hRichEdit);

/*
 * RESC_Detach
 *   Removes the subclass and frees the context.  Safe to call with NULL.
 *   Must be called before the RichEdit window is destroyed.
 */
void   RESC_Detach(HRESC h);

/*
 * RESC_SyncThumb
 *   Forces the scrollbar thumb to reflect the current RichEdit scroll position.
 *   Call after any programmatic scroll driven externally (EM_SETSCROLLPOS, etc.).
 *   Not needed for normal WM_VSCROLL or WM_MOUSEWHEEL — those are handled
 *   natively by the RichEdit which updates the thumb itself.
 */
void   RESC_SyncThumb(HRESC h);

/*
 * RESC_OnMouseWheel
 *   Call from the parent window's WM_MOUSEWHEEL handler, passing wParam
 *   unchanged.  Forwards WM_MOUSEWHEEL directly to the RichEdit so the
 *   control handles both the scroll and its own thumb update natively.
 */
LRESULT RESC_OnMouseWheel(HRESC h, WPARAM wParam);

#ifdef __cplusplus
}
#endif
