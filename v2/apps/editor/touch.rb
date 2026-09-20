# Touch editing: tap to place the cursor, drag to select, tap the gutter
# for a whole line, tap the status line for command mode, tap a command
# on the strip to run it.
#
# The reference editor this one learns from is keyboard-only. The
# hardware target is a tablet, so this is the half that actually matters
# there -- and it is the same command surface the keyboard drives, not a
# second one bolted on.
#
# Press and release are tracked explicitly rather than with the usual
# press-once-per-hold guard, because dragging is exactly the case that
# guard exists to suppress. The one-shot targets -- the strip and the
# status line -- keep the guard, via @tap_consumed.
#
# A mixin's methods resolve bare constants lexically, through its own
# nesting and ancestry, never through whatever class includes it (see
# layout.rb's comment -- this is exactly the bug that broke EditorCmd on
# its first ESC). So this module includes EditorLayout itself for
# TEXT_Y/STATUS_Y/GUTTER_W/TEXT_X/LINE_H/CHAR_W, and reaches EditorCmd's
# CMD_ROWS/CMD_CELL_CHARS by qualified name rather than including
# EditorCmd too -- EditorApp already includes EditorCmd, and including it
# a second time here would only muddy the ancestor chain.
module EditorTouch
  include EditorLayout

  def editor_touch(x, y, pressed)
    unless pressed
      @touch_down = false
      @tap_consumed = false
      return false
    end

    fresh = !@touch_down
    @touch_down = true

    # One-shot targets first, and only on the press that started the
    # hold: holding a finger on the strip must not re-run its command
    # every frame.
    if fresh
      return true if touch_command_strip(x, y)
      return true if touch_status(x, y)
    end
    return false if @tap_consumed
    return false if y < TEXT_Y || y >= STATUS_Y

    if x < GUTTER_W
      return touch_gutter(y, fresh)
    end
    touch_text(x, y, fresh)
  end

  def touch_command_strip(x, y)
    return false unless cmd_active?
    return false if cmd_prompt_active?
    sy = cmd_strip_y
    return false if y < sy || y >= STATUS_Y
    row = (y - sy) / LINE_H
    cells = EditorCmd::CMD_ROWS[row]
    return true if cells.nil?
    col = (x - 2) / (EditorCmd::CMD_CELL_CHARS * CHAR_W)
    cell = cells[col]
    if cell.nil? || col < 0
      cmd_close
    else
      cmd_close
      cmd_run(cell[0])
    end
    @tap_consumed = true
    true
  end

  def touch_status(x, y)
    return false if y < STATUS_Y
    if cmd_active?
      cmd_close
    else
      cmd_open
    end
    @tap_consumed = true
    true
  end

  def touch_gutter(y, fresh)
    row = (y - TEXT_Y) / LINE_H + @scroll_y
    return false if row >= @buf.line_count
    if fresh
      @buf.clear_mark
      @buf.set_cursor(0, row)
      @buf.toggle_mark
    end
    # Extending down the gutter selects whole lines: the mark stays at the
    # start of the first, the cursor runs to the end of the current.
    @buf.set_cursor(@buf.line(row).length, row)
    ensure_scroll
    true
  end

  def touch_text(x, y, fresh)
    row = (y - TEXT_Y) / LINE_H + @scroll_y
    row = @buf.line_count - 1 if row >= @buf.line_count
    col = (x - TEXT_X) / CHAR_W + @scroll_x
    col = 0 if col < 0
    if fresh
      @buf.clear_mark
      @buf.set_cursor(col, row)
      @drag_from = [@buf.cx, @buf.cy]
      ensure_scroll
      return true
    end
    # Continuing a hold: this is a drag, so anchor a mark at wherever the
    # press landed and let the cursor run.
    unless @buf.mark_set?
      @buf.set_cursor(@drag_from[0], @drag_from[1])
      @buf.toggle_mark
    end
    @buf.set_cursor(col, row)
    ensure_scroll
    true
  end
end
