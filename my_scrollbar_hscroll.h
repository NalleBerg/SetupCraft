/*
 * my_scrollbar_hscroll.h — Public header for the horizontal custom scrollbar.
 *
 * H-scroll is implemented in my_scrollbar_hscroll.cpp, which is #included
 * directly inside my_scrollbar_vscroll.cpp (shared compilation unit so it
 * has full access to MsbCtx, S(), and all internal helpers).
 *
 * The public API (msb_attach, msb_detach, msb_sync, etc.) is the same as
 * for the V-scroll — declared once in my_scrollbar_vscroll.h.
 *
 * USAGE
 * -----
 *   Files that attach an H-bar:
 *     #include "my_scrollbar_vscroll.h"   // declares the full API
 *     #include "my_scrollbar_hscroll.h"   // documents intent; pulls vscroll.h in
 *
 *   HMSB hH = msb_attach(hWnd, MSB_HORIZONTAL);
 *   HMSB hH = msb_attach(hWnd, MSB_HORIZONTAL | MSB_NOHIDE);
 *
 * STATUS
 * ------
 *   Step 1 (current): H-scroll suppressed — bar window not shown,
 *   native Windows H-bar suppressed.  Content does not scroll.
 *   Implementation lives in my_scrollbar_hscroll.cpp.
 */

#pragma once
#include "my_scrollbar_vscroll.h"
