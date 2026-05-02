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
 * DELIVERY STRATEGY
 * ------------------
 * ListView cannot use WM_HSCROLL(SB_THUMBPOSITION) because SIF_TRACKPOS is
 * only populated by the native scrollbar machinery, which we suppress.  Instead
 * we drive scrolling via LVM_SCROLL(delta, 0), which bypasses the scrollbar
 * subsystem entirely.  The inHDeliver flag guards against our own WM_NCPAINT
 * intercept corrupting the SCROLLINFO state mid-delivery (see MsbH_DeliverScroll).
 *
 * Non-ListView controls (TreeView, RichEdit) do not need LVM_SCROLL.  They
 * accept WM_HSCROLL(SB_THUMBPOSITION) with an up-to-date SCROLLINFO.nPos.
 */

/*
 * MsbH_DeliverScroll — scroll the target control to an absolute H-axis position.
 *
 * Called by Msb_ScrollToPos for every horizontal scroll event (tilt-wheel,
 * arrow buttons, track clicks, thumb drag).  Clamps newPos to [0, maxPos]
 * before applying the scroll.
 *
 * ListView path: uses LVM_SCROLL(delta) with pre-seeded SCROLLINFO.nPos = lvHPos
 * (see "Pre-seed" comment below).  Sets inHDeliver around the send so that the
 * WM_NCPAINT intercept skips Msb_HideNativeBar(H) and the SCROLLINFO restore,
 * both of which would otherwise corrupt the counter that LVM_SCROLL relies on.
 *
 * Non-ListView path: updates SCROLLINFO.nPos then sends WM_HSCROLL via origProc.
 */
static void MsbH_DeliverScroll(MsbCtx* ctx, int newPos)
{
    /* Clamp to valid scroll range. */
    if (ctx->isListView) {
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        Msb_FixListViewHScrollInfo(ctx->hTarget, &si);
        int maxPos = max(0, (int)(si.nMax - (int)si.nPage + 1));
        if (newPos < 0)      newPos = 0;
        if (newPos > maxPos) newPos = maxPos;

        int delta = newPos - ctx->lvHPos;
        if (delta == 0) return;

        /* Pre-seed SCROLLINFO.nPos = lvHPos (CURRENT position, not newPos).
         * LVM_SCROLL fires ShowScrollBar(SB_HORZ, TRUE) internally which
         * re-seeds the internal scroll counter from SCROLLINFO.nPos.
         * We must seed with lvHPos so that:
         *   counter = lvHPos, delta = newPos - lvHPos
         *   check: lvHPos + delta = newPos >= 0  (always valid after clamping)
         * Seeding with newPos instead would give counter = newPos and
         *   check: newPos + (newPos - lvHPos) = 2*newPos - lvHPos
         * which is negative for any leftward scroll beyond the midpoint.
         *
         * inHDeliver: (a) WM_NCPAINT skips Msb_HideNativeBar(H) so
         * ShowScrollBar(FALSE) cannot zero the counter mid-delivery, and
         * (b) restoreH in NCPAINT is skipped so it cannot overwrite our
         * pre-seeded nPos with a stale captured value. */
        SCROLLINFO setSi = {sizeof(setSi), SIF_POS};
        setSi.nPos = ctx->lvHPos;
        SetScrollInfo(ctx->hTarget, SB_HORZ, &setSi, FALSE);
        ctx->inHDeliver = TRUE;
        SendMessageW(ctx->hTarget, LVM_SCROLL, (WPARAM)delta, 0);
        ctx->inHDeliver = FALSE;
        ctx->lvHPos = newPos;
    } else {
        /* TreeView and RichEdit: WM_HSCROLL via origProc, same as the old path. */
        SCROLLINFO setSi = {sizeof(setSi), SIF_POS};
        setSi.nPos = newPos;
        SetScrollInfo(ctx->hTarget, SB_HORZ, &setSi, FALSE);
        MsbCtx* any = (MsbCtx*)GetPropW(ctx->hTarget, kMsbTargetPropV);
        if (!any) any = ctx;
        CallWindowProcW(any->origProc, ctx->hTarget, WM_HSCROLL,
                        MAKEWPARAM(SB_THUMBPOSITION, (WORD)newPos), 0);
        CallWindowProcW(any->origProc, ctx->hTarget, WM_HSCROLL,
                        MAKEWPARAM(SB_ENDSCROLL, 0), 0);
    }
}
