# System Monitor -- a paged window onto acid OS v2's own kernel/compositor/
# audio internals (not generic OS bookkeeping: there's no process table or
# heap allocator here worth showing, this OS's own distinctive machinery is
# its windows, its dirty-flag compositor, and its hand-built synth).
#
#   Page 1: WINDOWS -- every open window, [X] to close one (two clicks,
#           same "click again to confirm" safety as a real task manager)
#   Page 2: COMPOSITOR -- a live bar graph of composited vs. skipped
#           frames/sec over the last ~20 seconds
#   Page 3: SYNTH -- one box per voice, lit while it's sounding
#
# Tap the "<" / ">" arrows in the bottom nav bar to switch pages.
class SysMon < AcidApp
  WINDOW_W = 200
  WINDOW_H = 160
  TITLE_BAR_H = 16
  LINE_H = 11
  NAV_H = 12
  PAGES = [ :windows, :compositor, :synth ]
  HIST_LEN = 20

  BG_COLOR = 0x050607      # THEME_BG
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  MUTED_COLOR = 0x9DAAA3   # THEME_MUTED
  HARD_COLOR = 0x00FF66    # THEME_HARD
  PANEL_COLOR = 0x0B1712   # THEME_PANEL

  BTN_W = 30
  ROW_H = 12

  def on_create
    @page = 0
    @kill_armed = nil
    @hist_composited = []
    @hist_skipped = []
    @prev_composited = acid_composited_frames
    @prev_skipped = acid_skipped_frames
    @next_sample_at = Time.now.to_f + 1.0
    @row_indices = []
    redraw
  end

  def content_bottom
    WINDOW_H - NAV_H
  end

  def on_idle
    sample_history
    redraw
  end

  # Once a second (matching a real "per-second" rate stat), turns the two
  # ever-growing cumulative counters kernel_router.c tracks into a delta --
  # the actual thing worth graphing -- and keeps the last HIST_LEN of them.
  def sample_history
    now = Time.now.to_f
    return if now < @next_sample_at
    @next_sample_at = now + 1.0
    cur_c = acid_composited_frames
    cur_s = acid_skipped_frames
    @hist_composited << (cur_c - @prev_composited)
    @hist_composited.shift if @hist_composited.length > HIST_LEN
    @hist_skipped << (cur_s - @prev_skipped)
    @hist_skipped.shift if @hist_skipped.length > HIST_LEN
    @prev_composited = cur_c
    @prev_skipped = cur_s
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    if @page == 0
      draw_windows_page
    elsif @page == 1
      draw_compositor_page
    else
      draw_synth_page
    end
    draw_nav
    acid_draw_window_border
  end

  def open_windows
    list = []
    i = 0
    max = acid_window_max
    while i < max
      info = acid_window_info(i)
      list << [ i, info ] if info
      i += 1
    end
    list
  end

  # @row_indices has exactly one entry per drawn row, in the same order --
  # including the focused row, as nil -- so on_touch's row-number hit test
  # (computed straight from y, the same way every row's y was) can index
  # straight into it. Only pushing entries for rows that actually GOT a
  # close button (skipping the focused one) was tried first and was a
  # real bug: it silently shifted every row after the focused one out of
  # alignment with what @row_indices thought that row number meant, so a
  # tap on any close button below the focused row landed on the wrong
  # window's index or (confirmed live) out of bounds entirely, taking no
  # action at all.
  def draw_windows_page
    y = TITLE_BAR_H + 2
    acid_draw_text("WINDOWS", 2, y, MUTED_COLOR, BG_COLOR)
    y += LINE_H
    @row_indices = []
    open_windows.each do |index, info|
      break if y + ROW_H > content_bottom
      focused = info[5]
      label = (focused ? "> " : "  ") + short_name(info[0])
      color = focused ? HARD_COLOR : TEXT_COLOR
      acid_draw_text(label[0, 22], 2, y + 2, color, BG_COLOR)
      if focused
        @row_indices << nil
      else
        draw_close_button(index, y)
        @row_indices << index
      end
      y += ROW_H
    end
  end

  def draw_close_button(index, y)
    armed = @kill_armed == index
    label = armed ? "sure?" : "close"
    bg = armed ? HARD_COLOR : PANEL_COLOR
    fg = armed ? 0x050607 : MUTED_COLOR
    x = WINDOW_W - BTN_W - 4
    acid_fill_rect(x, y, BTN_W, ROW_H - 1, bg)
    acid_draw_text(label, x + 2, y + 2, fg, bg)
  end

  def draw_compositor_page
    y = TITLE_BAR_H + 2
    acid_draw_text("COMPOSITOR", 2, y, MUTED_COLOR, BG_COLOR)
    y += LINE_H
    cur_c = @hist_composited[-1] || 0
    cur_s = @hist_skipped[-1] || 0
    acid_draw_text("frames/s: #{cur_c}  skip/s: #{cur_s}", 2, y, TEXT_COLOR, BG_COLOR)
    y += LINE_H + 2
    draw_bar_graph(y, content_bottom - y, @hist_composited, @hist_skipped)
  end

  # Two-series bar graph, one column per second sampled -- composited
  # frames (bright) stacked on top of skipped ones (dim), scaled to
  # whichever second in the window had the busiest total. Bars, not the
  # reference monitor's line chart, to match this OS's own blocky look
  # (Breakout/Tetris draw the same way) rather than copying its style
  # wholesale.
  def draw_bar_graph(y0, h, composited, skipped)
    return if h <= 4
    peak = 1
    i = 0
    while i < composited.length
      total = composited[i] + skipped[i]
      peak = total if total > peak
      i += 1
    end
    col_w = 8
    x = 2
    i = 0
    while i < composited.length
      c = composited[i]
      s = skipped[i]
      c_h = c * (h - 1) / peak
      s_h = s * (h - 1) / peak
      bar_bottom = y0 + h
      acid_fill_rect(x, bar_bottom - s_h - c_h, col_w - 1, s_h, MUTED_COLOR) if s_h > 0
      acid_fill_rect(x, bar_bottom - c_h, col_w - 1, c_h, HARD_COLOR) if c_h > 0
      x += col_w
      i += 1
    end
  end

  def draw_synth_page
    y = TITLE_BAR_H + 2
    acid_draw_text("SYNTH", 2, y, MUTED_COLOR, BG_COLOR)
    y += LINE_H
    active = acid_active_voice_count
    acid_draw_text("#{active}/8 voices active", 2, y, TEXT_COLOR, BG_COLOR)
    y += LINE_H + 4
    box = 18
    gap = 4
    i = 0
    while i < 8
      x = 2 + i * (box + gap)
      on = i < active
      acid_fill_rect(x, y, box, box, on ? HARD_COLOR : PANEL_COLOR)
      i += 1
    end
  end

  def draw_nav
    y = content_bottom
    acid_fill_rect(0, y, WINDOW_W, NAV_H, PANEL_COLOR)
    acid_draw_text("<", 4, y + 2, TEXT_COLOR, PANEL_COLOR)
    acid_draw_text(">", WINDOW_W - 10, y + 2, TEXT_COLOR, PANEL_COLOR)
    label = "#{@page + 1}/#{PAGES.length}"
    acid_draw_text(label, (WINDOW_W - label.length * 6) / 2, y + 2, MUTED_COLOR, PANEL_COLOR)
  end

  def on_touch(x, y, pressed)
    unless pressed
      @touch_held = false
      return
    end
    return if @touch_held
    @touch_held = true

    if y >= content_bottom
      if x < 20
        turn_page((@page - 1 + PAGES.length) % PAGES.length)
      elsif x > WINDOW_W - 20
        turn_page((@page + 1) % PAGES.length)
      end
      return
    end

    return unless @page == 0
    row = (y - TITLE_BAR_H - LINE_H) / ROW_H
    return if row < 0 || row >= @row_indices.length
    return if x < WINDOW_W - BTN_W - 4
    index = @row_indices[row]
    return if index.nil?
    if @kill_armed == index
      acid_close_window(index)
      @kill_armed = nil
    else
      @kill_armed = index
    end
    redraw
  end

  def turn_page(page)
    @page = page
    @kill_armed = nil
    redraw
  end

  def short_name(app_name)
    slash = app_name.rindex("/")
    base = slash ? app_name[slash + 1, app_name.length - slash - 1] : app_name
    base.sub(/\.rb$/, "")
  end
end

SysMon.new.start
