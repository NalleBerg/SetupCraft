/*
 * my_scrollbar.h — Custom Win32 scrollbar for RichEdit and any SCROLLINFO window.
 *
 * USAGE
 * -----
 *   #include "my_scrollbar.h"
 *
 *   // Vertical, fades in/out on hover (default):
 *   HMSB hSB = msb_attach(hWnd, MSB_VERTICAL);
 *
 *   // Vertical, always visible:
 *   HMSB hSB = msb_attach(hWnd, MSB_VERTICAL | MSB_NOHIDE);
 *
 *   // Horizontal, always visible:
 *   HMSB hSB = msb_attach(hWnd, MSB_HORIZONTAL | MSB_NOHIDE);
 *
 *   // On parent WM_DESTROY:
 *   msb_detach(hSB);
 *
 * The attach call automatically:
 *   - Hides the native scrollbar (ShowScrollBar) but keeps WS_VSCROLL /
 *     WS_HSCROLL so the target window still maintains SCROLLINFO internally.
 *   - Creates the custom scrollbar as a child of the target window.
 *   - Subclasses the target to intercept WM_SIZE, WM_VSCROLL/WM_HSCROLL,
 *     WM_MOUSEWHEEL, and WM_DESTROY.
 *
 * No other calls are needed in normal use.
 * Call msb_sync(hSB) after any programmatic scroll that does not send
 * WM_VSCROLL/WM_HSCROLL (e.g. direct EM_SCROLL calls from your own code).
 *
 * NO SetupCraft dependencies — only <windows.h>.
 */

#pragma once
#include <windows.h>

/* ── Opaque handle ──────────────────────────────────────────────────────────*/

typedef void* HMSB;

/* ── Flags (combine with |) ─────────────────────────────────────────────────*/

#define MSB_VERTICAL    0x00u   /* place bar on the right edge (default)      */
#define MSB_HORIZONTAL  0x01u   /* place bar on the bottom edge               */
#define MSB_NOHIDE      0x02u   /* always fully visible; no fade animation    */

/* ── Colors — change all visuals here ──────────────────────────────────────*/

/* Bar background */
#define MSB_CLR_TRACK       RGB(235, 238, 242)

/* Arrow buttons — tinted to match thumb state */
#define MSB_CLR_ARROW_BG        RGB(190, 210, 228)  /* matches idle blue   */
#define MSB_CLR_ARROW_BG_HOVER  RGB(104, 188, 108)  /* matches hover green */
#define MSB_CLR_ARROW_BG_PRESS  RGB(140, 140, 148)  /* matches drag gray   */
#define MSB_CLR_ARROW_GLYPH     RGB( 60,  60,  80)

/* Thumb
 *   THUMB_NORMAL : bleach blue  — idle, no interaction
 *   THUMB_HOVER  : bleach green — mouse is over the bar
 *   THUMB_DRAG   : mid gray     — actively scrolling (drag, arrow click,
 *                                 page click, or mouse wheel) */
#define MSB_CLR_THUMB           RGB(160, 196, 222)  /* bleach blue  */
#define MSB_CLR_THUMB_HOVER     RGB(128, 208, 130)  /* bleach green */
#define MSB_CLR_THUMB_DRAG      RGB(140, 140, 148)  /* mid gray     */

/* ── Layout constants (96-DPI baseline; scaled internally by DPI) ───────────*/

#define MSB_WIDTH_FULL      12  /* px at 96 DPI — full visible width          */
#define MSB_WIDTH_HIDDEN     3  /* px at 96 DPI — hint strip in hidden mode  */
#define MSB_ARROW_HEIGHT    16  /* px at 96 DPI — height of each arrow button */
#define MSB_THUMB_MIN       20  /* px at 96 DPI — minimum thumb height        */
#define MSB_CORNER_RADIUS    4  /* px at 96 DPI — rounded corner radius       */
#define MSB_VERT_MARGIN      6  /* px at 96 DPI — breathing room below last
                                   line of a RichEdit (format rect shrinkage) */

/* ── Public API ─────────────────────────────────────────────────────────────*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 * msb_attach — attach a custom scrollbar to hTarget.
 *
 * flags:  combination of MSB_VERTICAL / MSB_HORIZONTAL / MSB_NOHIDE.
 * returns an opaque HMSB handle, or NULL on failure.
 *
 * Call once per axis.  Two handles for both axes is fine:
 *   HMSB hV = msb_attach(hWnd, MSB_VERTICAL   | MSB_NOHIDE);
 *   HMSB hH = msb_attach(hWnd, MSB_HORIZONTAL | MSB_NOHIDE);
 */
HMSB msb_attach(HWND hTarget, DWORD flags);

/*
 * msb_detach — detach and destroy the custom scrollbar.
 * Restores the native scrollbar and the original window proc.
 * Safe to call with NULL.
 */
void msb_detach(HMSB h);

/*
 * msb_sync — manually sync the thumb to the current scroll position.
 * Call this after programmatic scrolls that bypass WM_VSCROLL / WM_HSCROLL,
 * e.g. after loading new content into a RichEdit that resets scroll position.
 */
void msb_sync(HMSB h);

/*
 * msb_notify_content_changed — invalidate the cached content-width measurement
 * and re-sync.  Call from EN_CHANGE (or equivalent) whenever text is edited so
 * the horizontal bar thumb correctly reflects the new document width.
 */
void msb_notify_content_changed(HMSB h);

/*
 * msb_set_insets — restrict the bar window to a sub-range of the target's edge.
 * insetNear: pixels to skip from the near edge (top for vertical, left for horizontal).
 * insetFar : pixels to skip from the far  edge (bottom for vertical, right for horizontal).
 * Use this when the target is a top-level window and the bar should not overlap
 * fixed chrome (e.g. toolbar at top, status bar at bottom).
 */
void msb_set_insets(HMSB h, int insetNear, int insetFar);

/*
 * msb_get_bar_hwnd — return the bar HWND so the caller can exclude it from
 * child-moving loops (e.g. when the target is a top-level window whose page
 * scroll handler moves all children by -dy).
 */
HWND msb_get_bar_hwnd(HMSB h);

/*
 * msb_set_edge_gap — shift the bar inward from the window edge by `gap` px.
 * For a vertical bar this moves it left; for horizontal, upward.
 * Use to make the hint strip slightly more visible against the page background.
 */
void msb_set_edge_gap(HMSB h, int gap);

/*
 * msb_reposition — re-derive the bar's position and size from the target
 * window's current client rect (via ClientToScreen / GetClientRect) and
 * recompute the thumb from the current SCROLLINFO.  Call this explicitly
 * after any programmatic resize of the target (e.g. after DeferWindowPos /
 * SetWindowPos on the target) to guarantee the bar tracks the target's new
 * corners immediately, without relying on the target's own WM_SIZE delivery.
 */
void msb_reposition(HMSB h);

#ifdef __cplusplus
}
#endif
