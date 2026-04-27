/*
 * my_scrollbar_hscroll.cpp — ListView horizontal-scroll delivery.
 *
 * #included directly by my_scrollbar_vscroll.cpp (not compiled separately).
 * Has full access to all internal types (MsbCtx, S(), Msb_Fix*, etc.).
 *
 * GOLDEN RULES
 * ------------
 * 1. lvHPos is the AUTHORITATIVE horizontal position. Written ONLY by:
 *      a. WM_LBUTTONDOWN drag-start  (header is idle — correct ground truth)
 *      b. MsbH_DeliverScroll()       (sets lvHPos = clamped newPos)
 *      c. WM_SIZE / msb_reposition   (layout settle — header is correct)
 *    NEVER re-read lvHPos from the header mid-drag / mid-scroll:
 *    LVM_SCROLL → WM_NCPAINT → ShowScrollBar(FALSE) removes WS_HSCROLL
 *    → next LVM_SCROLL silently does nothing → header never moves → re-read = 0.
 *
 * 2. LVM_SCROLL(dx, 0) is the only reliable H-scroll primitive.
 *    WM_HSCROLL(SB_THUMBPOSITION) needs SIF_TRACKPOS (set only by native
 *    scrollbar machinery).  SetScrollInfo + SB_THUMBTRACK zeroes the delta
 *    because ListView reads nPos AFTER we updated it.
 *
 * 3. WS_HSCROLL is removed by ShowScrollBar(FALSE) in our WM_NCPAINT intercept.
 *    After removal, LVM_SCROLL silently does nothing.
 *    Fix in later steps: ShowScrollBar(SB_HORZ, TRUE) just before LVM_SCROLL,
 *    then let NCPAINT remove it again.  Guard SCROLLINFO capture/restore so
 *    the NCPAINT intercept does not corrupt lvHPos.
 *
 * DEVELOPMENT STEPS
 * -----------------
 * Step 1 (current): suppress native bar only.
 *   Bar draws, thumb tracks position, native bar hidden.  Content does NOT
 *   scroll yet — that is intentional.  Verify the bar looks correct.
 *
 * Step 2: add arrow-button scrolling (simplest path).
 * Step 3: add track-click scrolling.
 * Step 4: add thumb drag.
 *
 * See my_scrollbar_hscroll_template.cpp for the previous implementation.
 */

/* ── Step 1: suppress native bar, track position, do not scroll yet ─────── */

/*
 * MsbH_DeliverScroll — called by Msb_ScrollToPos for ListView H-axis.
 *
 * Currently: clamps newPos, updates lvHPos (so the thumb redraws in the
 * correct position), but does NOT send LVM_SCROLL.  Content stays still.
 * This lets us confirm the bar draws correctly before adding scroll delivery.
 */
static void MsbH_DeliverScroll(MsbCtx* ctx, int newPos)
{
    (void)ctx;
    (void)newPos;
    /* Step 1: suppress all H-scroll everywhere.  Nothing moves.
     * Bar draws, native bar hidden — verify that before adding scroll in Step 2. */
}
