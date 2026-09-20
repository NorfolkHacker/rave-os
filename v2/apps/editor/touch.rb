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
      # Not load-bearing today (every fresh press re-initialises this
      # anyway, via `fresh ||` in touch_gutter), but leaving a gesture
      # flag set across gestures is exactly the shape that caused the
      # stale-mark bug this flag exists to prevent -- clear it here too
      # so there is never a lingering "anchored" claim from a gesture
      # that has already ended.
      @gutter_anchored = false
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
    # A prompt (find/goto/save-as) is modal: cmd_prompt_submit reads
    # @buf.cx/@buf.cy to decide where the pending command acts from, so a
    # tap that quietly moved the cursor underneath it would corrupt that
    # command with no visible sign anything happened, and a drag would
    # toggle a mark underneath the modal too. Ignoring taps in the
    # text/gutter while a prompt is active is simplest, and matches the
    # existing rule that the strip itself also refuses a tap while a
    # prompt is up (touch_command_strip, above).
    return false if cmd_prompt_active?

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
    # Deliberately NOT guarded by cmd_prompt_active?, unlike the strip,
    # text and gutter. On the target hardware there is no keyboard and
    # so no ESC: a tap on the status line is the ONLY way to cancel a
    # find/goto/save-as prompt that was opened by tapping. Blocking this
    # target while a prompt is active would make prompts unescapable on
    # the device this editor is for.
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
    # Same dead band as touch_text: the gutter only ever DRAWS
    # visible_lines rows (draw_gutter stops at that same limit), so a tap
    # in the sliver just above STATUS_Y can compute a row that is real
    # (< line_count, so the check below alone wouldn't catch it) but was
    # never on screen. Clamp to the last visible row BEFORE the
    # line_count check, same order as touch_text, so a tap there selects
    # the last line the user could actually see instead of one further
    # down that they couldn't.
    max_visible_row = @scroll_y + visible_lines - 1
    row = max_visible_row if row > max_visible_row
    # The out-of-range bounds check has to come before we can decide
    # whether this row is usable, but it must NOT be allowed to skip
    # initialisation for the rest of the hold: a hold's first event can
    # land below the last line (row invalid, we return here) while a
    # later event in the SAME hold lands on a real row with `fresh`
    # already false. Without @gutter_anchored, that later event would
    # fall straight into the "extend" branch below and reuse whatever
    # mark a previous, unrelated gesture left behind -- selections
    # deliberately persist across releases, so a stale one is sitting
    # right there waiting to be extended by a gesture that never
    # anchored its own.
    if row >= @buf.line_count
      @gutter_anchored = false if fresh
      return false
    end
    if fresh || !@gutter_anchored
      @buf.clear_mark
      @buf.set_cursor(0, row)
      @buf.toggle_mark
      @gutter_anchored = true
    end
    # Extending down the gutter selects whole lines: the mark stays at the
    # start of the first, the cursor runs to the end of the current.
    @buf.set_cursor(@buf.line(row).length, row)
    ensure_scroll
    true
  end

  def touch_text(x, y, fresh)
    row = (y - TEXT_Y) / LINE_H + @scroll_y
    # A window this tall only ever DRAWS visible_lines rows before
    # STATUS_Y; a few px of rounding in the sliver just above the status
    # line computes a real but undrawn line index (visible_lines rows
    # means rows scroll_y..scroll_y+visible_lines-1 are ever on screen).
    # Clamping to line_count alone isn't enough on a buffer longer than
    # the screen -- the computed row is still a valid line, just one
    # nobody can see, and landing the cursor there makes ensure_scroll
    # yank the whole viewport from a tap that looked like it hit nothing.
    max_visible_row = @scroll_y + visible_lines - 1
    row = max_visible_row if row > max_visible_row
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
