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
#define MSB_CLR_TRACK       RGB(240, 240, 240)

/* Arrow buttons */
#define MSB_CLR_ARROW_BG        RGB(205, 205, 205)
#define MSB_CLR_ARROW_BG_HOVER  RGB(180, 180, 180)
#define MSB_CLR_ARROW_BG_PRESS  RGB(130, 130, 130)
#define MSB_CLR_ARROW_GLYPH     RGB( 80,  80,  80)

/* Thumb */
#define MSB_CLR_THUMB           RGB(180, 180, 180)
#define MSB_CLR_THUMB_HOVER     RGB(140, 140, 140)
#define MSB_CLR_THUMB_DRAG      RGB( 90, 130, 190)  /* accent blue while dragging */

/* ── Layout constants (96-DPI baseline; scaled internally by DPI) ───────────*/

#define MSB_WIDTH_FULL      12  /* px at 96 DPI — full visible width          */
#define MSB_WIDTH_HIDDEN     3  /* px at 96 DPI — thin strip in hidden mode   */
#define MSB_ARROW_HEIGHT    16  /* px at 96 DPI — height of each arrow button */
#define MSB_THUMB_MIN       20  /* px at 96 DPI — minimum thumb height        */
#define MSB_CORNER_RADIUS    4  /* px at 96 DPI — rounded corner radius       */

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

#ifdef __cplusplus
}
#endif
