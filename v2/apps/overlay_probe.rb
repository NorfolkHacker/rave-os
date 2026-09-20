# Manual test fixture for the kernel overlay (core/kernel/kernel_overlay.h),
# not a user-facing app -- `menu = false`, launched with `run overlay_probe`
# from the terminal. Paints one green bar and one key-coloured hole inside
# it across the middle of the screen and leaves them there until its window
# is closed, which is long enough to drag other windows over and under the
# area and confirm three things by eye: the bar draws on top of every
# window, the hole shows whatever is really underneath, and clicking on the
# bar hits whatever window is beneath it rather than the overlay.
class OverlayProbeApp < AcidApp
  BAR_X = 120
  BAR_Y = 140
  BAR_W = 400
  BAR_H = 80
  HOLE_INSET = 30

  BAR_COLOR = 0x00FF66   # THEME_HARD
  KEY_COLOR = 0xFF00FF   # ACID_OVERLAY_KEY -- must match kernel_theme.h

  def on_create
    @opened = acid_overlay_open
    return unless @opened
    acid_overlay_clear
    acid_overlay_fill_rect(BAR_X, BAR_Y, BAR_W, BAR_H, BAR_COLOR)
    acid_overlay_fill_rect(BAR_X + HOLE_INSET, BAR_Y + HOLE_INSET,
                           BAR_W - 2 * HOLE_INSET, BAR_H - 2 * HOLE_INSET,
                           KEY_COLOR)
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    acid_draw_text(@opened ? "overlay open" : "overlay failed", 6, 24, 0xD4E6DB, 0x050607)
    acid_draw_text("close me to clear", 6, 36, 0x9DAAA3, 0x050607)
    acid_draw_window_border
  end

  def on_destroy
    acid_overlay_close
  end
end

OverlayProbeApp.new.start
