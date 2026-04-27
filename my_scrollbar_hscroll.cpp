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
 * Step 1 (done): suppress native bar only.
 *   Bar draws, thumb tracks position, native bar hidden.  Content does NOT
 *   scroll yet — baseline verified.
 *
 * Step 2 (partial): tilt-wheel delivery via LVM_SCROLL.
 *   Right-scroll works.  Left-scroll broken: origProc's WM_NCPAINT overwrites
 *   SetScrollInfo(nPos=lvHPos) with nPos=0; LVM_SCROLL then clamps leftward
 *   delta to 0.  Fix direction: WM_HSCROLL(SB_THUMBPOSITION)+SIF_TRACKPOS=newPos.
 *
 * Step 3: show bar (remove MSB_HORIZONTAL visibility guards).
 * Step 4: arrow-button scrolling.
 * Step 5: track-click scrolling.
 * Step 6: thumb drag.
 */

/* ── Step 2: LVM_SCROLL delivery (tilt-wheel right works; left pending) ─── */

/*
 * MsbH_DeliverScroll — called by Msb_ScrollToPos for all H-axis targets.
 *
 * ListView: clamps newPos, sets full SCROLLINFO, re-enables WS_HSCROLL, sends
 * LVM_SCROLL.  Right-scroll works.  Left-scroll is still broken — origProc's
 * WM_NCPAINT overwrites nPos=lvHPos back to 0 before LVM_SCROLL runs.
 * Fix direction: replace LVM_SCROLL(delta) with WM_HSCROLL(SB_THUMBPOSITION)
 * + SIF_TRACKPOS=newPos (absolute, no delta-clamping).
 *
 * Non-ListView: WM_HSCROLL(SB_THUMBPOSITION) via origProc.
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

        /* The NCPAINT intercept fires synchronously inside ShowScrollBar(TRUE).
         * It captures SCROLLINFO → calls ShowScrollBar(FALSE) → restores SCROLLINFO.
         * BUT: if nMax=0 when captured (zeroed by the previous ShowScrollBar(FALSE)),
         * restoreH=FALSE and nPos is never restored.  LVM_SCROLL then sees nPos=0,
         * treats the current position as 0, and refuses any negative (leftward) delta.
         *
         * Fix: set the full SCROLLINFO (range + current position) BEFORE calling
         * ShowScrollBar(TRUE), so the NCPAINT capture sees nMax>0 (restoreH=TRUE)
         * and nPos=lvHPos, and restores both correctly.  LVM_SCROLL then knows the
         * real current position and can scroll in both directions. */
        SCROLLINFO fullSi = {sizeof(fullSi), SIF_RANGE | SIF_PAGE | SIF_POS};
        fullSi.nMin  = si.nMin;
        fullSi.nMax  = si.nMax;
        fullSi.nPage = si.nPage;
        fullSi.nPos  = ctx->lvHPos;
        SetScrollInfo(ctx->hTarget, SB_HORZ, &fullSi, FALSE);

        ShowScrollBar(ctx->hTarget, SB_HORZ, TRUE);
        SendMessageW(ctx->hTarget, LVM_SCROLL, (WPARAM)delta, 0);
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
