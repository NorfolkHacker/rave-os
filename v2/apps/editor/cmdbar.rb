# Command mode: ESC raises a strip of single-key commands over the bottom
# of the text area, one keypress runs one, ESC closes it.
#
# Not a menu bar, and not Ctrl/Alt chords, because neither is reachable
# here: hal_input_sim.cpp's translate_scancode drops Ctrl, Alt and the
# function keys outright, and the hardware target is a tablet with no
# keyboard to press them on anyway. A strip of plain letters is the one
# surface that works identically typed and tapped -- see the design doc.
#
# Prompts (find, goto, save-as) arrive in Task 7; this task is the strip
# and the commands that act immediately.
module EditorCmd
  # Three fixed rows, not a paging list: at 65 columns everything fits
  # with room to spare, and a fixed strip means a command never moves,
  # which is what makes the tap targets (Task 11) learnable.
  CMD_ROWS = [
    [["s", "save"], ["a", "save-as"], ["q", "close"], ["!", "run"],
     ["u", "undo"], ["r", "redo"]],
    [["x", "cut"], ["c", "copy"], ["v", "paste"], ["m", "mark"],
     ["/", "find"], ["n", "next"]],
    [["g", "goto"], ["t", "top"], ["b", "bottom"], ["h", "hilite"],
     ["p", "prev"], ["?", "keys"]]
  ]

  CMD_CELL_CHARS = 10
  CMD_BG = 0x123322    # THEME_PANEL's documented button-hover shade
  CMD_KEY_COLOR = 0x00FF66   # THEME_HARD
  CMD_TEXT_COLOR = 0xD4E6DB  # THEME_TEXT

  def cmd_strip_y
    STATUS_Y - CMD_ROWS.length * LINE_H
  end

  def cmd_active?
    @cmd_open ? true : false
  end

  def cmd_open
    @cmd_open = true
    @message = nil
  end

  def cmd_close
    @cmd_open = false
  end

  # Returns true when the key was consumed, so on_key can stop. An
  # unrecognised key closes the strip rather than sitting there swallowing
  # input -- a command surface you can get stuck inside is worse than one
  # you occasionally have to reopen.
  def cmd_key(code)
    return false unless cmd_active?
    if code == AcidKeys::ESCAPE
      cmd_close
      return true
    end
    cmd_close
    return true if code < 32 || code > 126
    cmd_run(code.chr)
    true
  end

  def cmd_run(ch)
    if ch == "s"
      save_file
    elsif ch == "u"
      @message = "nothing to undo" unless @buf.undo
    elsif ch == "r"
      @message = "nothing to redo" unless @buf.redo
    elsif ch == "m"
      @buf.toggle_mark
      @message = @buf.mark_set? ? "mark set" : "mark cleared"
    elsif ch == "c"
      @message = @buf.copy ? "copied" : "no selection"
    elsif ch == "x"
      @message = @buf.cut ? "cut" : "no selection"
    elsif ch == "v"
      @message = @buf.paste ? "pasted" : "clipboard empty"
    elsif ch == "t"
      @buf.set_cursor(0, 0)
    elsif ch == "b"
      @buf.set_cursor(0, @buf.line_count - 1)
    else
      @message = "no command '#{ch}'"
    end
    ensure_scroll
  end

  def draw_cmd_strip
    y = cmd_strip_y
    acid_fill_rect(0, y, WINDOW_W, CMD_ROWS.length * LINE_H, CMD_BG)
    row = 0
    while row < CMD_ROWS.length
      cells = CMD_ROWS[row]
      i = 0
      while i < cells.length
        x = 2 + i * CMD_CELL_CHARS * CHAR_W
        acid_draw_text(cells[i][0], x, y + row * LINE_H + 1, CMD_KEY_COLOR, CMD_BG)
        acid_draw_text(cells[i][1], x + 2 * CHAR_W, y + row * LINE_H + 1,
                       CMD_TEXT_COLOR, CMD_BG)
        i += 1
      end
      row += 1
    end
  end
end
