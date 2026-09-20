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
    @prompt = nil
    @prompt_text = nil
  end

  # A prompt keeps the strip up and takes text on the status row. The
  # three that need text are find, goto and save-as; everything else acts
  # on the keypress.
  def cmd_prompt_active?
    !@prompt.nil?
  end

  def cmd_prompt_open(kind, label)
    @prompt = kind
    @prompt_label = label
    @prompt_text = ""
    @cmd_open = true
  end

  def cmd_prompt_key(code)
    return false unless cmd_prompt_active?
    if code == AcidKeys::ESCAPE
      cmd_close
    elsif code == AcidKeys::ENTER
      text = @prompt_text
      kind = @prompt
      cmd_close
      cmd_prompt_submit(kind, text)
    elsif code == AcidKeys::BACKSPACE
      @prompt_text = @prompt_text[0, @prompt_text.length - 1] if @prompt_text.length > 0
    elsif code >= 32 && code <= 126
      @prompt_text = @prompt_text + code.chr
    end
    true
  end

  def cmd_prompt_submit(kind, text)
    if kind == :find
      return if text.length == 0
      @last_query = text
      find_from(@buf.cx + 1, @buf.cy)
    elsif kind == :goto
      n = text.to_i
      if n < 1 || n > @buf.line_count
        @message = "no line #{text}"
      else
        @buf.set_cursor(0, n - 1)
      end
    elsif kind == :saveas
      return if text.length == 0
      @path = text
      save_file
    end
    ensure_scroll
  end

  # Search forward from a position, wrapping, and move there. Separate
  # from cmd_prompt_submit so find-next and find-prev reuse it without
  # reopening the prompt.
  def find_from(x, y)
    hit = @buf.find(@last_query, x, y)
    if hit.nil?
      @message = "not found: #{@last_query}"
      return
    end
    @buf.set_cursor(hit[0], hit[1])
    @message = @last_query
  end

  def find_prev_from(x, y)
    # No backward search in Buffer on purpose: with wraparound, the
    # previous match is just "the last match reached by scanning forward
    # from here", and one search direction is one thing to get right.
    return @message = "no search yet" if @last_query.nil?
    best = nil
    pos = [0, 0]
    n = 0
    while n < 10000
      hit = @buf.find(@last_query, pos[0], pos[1])
      break if hit.nil?
      break if !best.nil? && hit[0] == best[0] && hit[1] == best[1]
      break if hit[1] > y || (hit[1] == y && hit[0] >= x)
      best = hit
      pos = [hit[0] + 1, hit[1]]
      n += 1
    end
    if best.nil?
      @message = "no earlier match"
      return
    end
    @buf.set_cursor(best[0], best[1])
    @message = @last_query
  end

  def draw_cmd_prompt
    acid_fill_rect(0, STATUS_Y, WINDOW_W, LINE_H, CMD_BG)
    text = "#{@prompt_label}: #{@prompt_text}_"
    acid_draw_text(text[0, WINDOW_W / CHAR_W - 1], 2, STATUS_Y + 1,
                   CMD_TEXT_COLOR, CMD_BG)
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
    was_armed = @quit_armed
    @quit_armed = false
    @quit_armed = was_armed if ch == "q"
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
    elsif ch == "/"
      cmd_prompt_open(:find, "find")
    elsif ch == "n"
      if @last_query.nil?
        @message = "no search yet"
      else
        find_from(@buf.cx + 1, @buf.cy)
      end
    elsif ch == "p"
      find_prev_from(@buf.cx, @buf.cy)
    elsif ch == "g"
      cmd_prompt_open(:goto, "line")
    elsif ch == "a"
      cmd_prompt_open(:saveas, "save as")
    elsif ch == "q"
      cmd_quit
    elsif ch == "?"
      @message = "ESC then a letter; see the strip"
    else
      @message = "no command '#{ch}'"
    end
    ensure_scroll
  end

  # Two presses to lose unsaved work, and the second one has to be the
  # same key -- a status-line confirmation rather than a dialog, because
  # this app framework has no dialog concept and a confirmation needs
  # none.
  #
  # Closing is "just return from the script" (see acid_app.rb's quit!):
  # vm_host.c unregisters the window, frees the canvas, clears focus,
  # deletes the queue and deletes the task on every exit path already,
  # including a normal script end, so there is no window handle for this
  # app to close itself by. acid_close_window takes a window INDEX and
  # deliberately refuses to close the caller's own window (window_binding.c)
  # -- it's for one app closing another, like sysmon, not this.
  def cmd_quit
    if !@buf.modified? || @quit_armed
      quit!
      return
    end
    @quit_armed = true
    @message = "unsaved -- ESC q again to close"
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
