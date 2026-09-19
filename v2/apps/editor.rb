class EditorApp < AcidApp
  # Must match the kernel_spawn_app(...) call that spawns this app (Task 7)
  # and kernel_layout.h's KERNEL_TITLE_BAR_H.
  WINDOW_W = 240
  WINDOW_H = 170
  TITLE_BAR_H = 16
  LINE_H = 10
  CHAR_W = 6
  DEFAULT_FILE = "v2/fsroot/Home/notes.txt"

  # Left gutter showing line numbers -- 4 digits is plenty for anything
  # this OS's apps run to (the longest file in v2/apps so far is under
  # 300 lines); a file longer than 9999 lines just loses its rightmost
  # digit, same graceful-truncation approach every fixed-width label in
  # this codebase already takes rather than reflowing layout for an edge
  # case that doesn't come up in practice here.
  GUTTER_CHARS = 4
  GUTTER_W = GUTTER_CHARS * CHAR_W
  TEXT_X = GUTTER_W + 2

  BG_COLOR = 0x0B1712      # THEME_PANEL -- status line
  BODY_BG = 0x050607       # THEME_BG -- text body
  GUTTER_BG = 0x050607     # THEME_BG
  GUTTER_COLOR = 0x9DAAA3  # THEME_MUTED
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  CURSOR_COLOR = 0x00FF66  # THEME_HARD
  STATUS_COLOR = 0x9DAAA3  # THEME_MUTED

  def on_create
    # File Manager launches this with a specific file to view/edit
    # (clicking a .rb file spawns Editor with that path as the launch
    # arg -- see file_manager.rb's activate_selected); opened directly
    # from Menu with no arg, it falls back to the general notes file it
    # always edited before. Either way the file is genuinely editable
    # and saveable, including files under fsroot/App (a real symlink to
    # v2/apps) -- there's no separate read-only mode, by design: this is
    # meant to double as a live way to tweak an app's own source and see
    # the change on its next launch, no rebuild step.
    arg = acid_launch_arg
    @path = arg.empty? ? DEFAULT_FILE : arg
    load_file
    @cx = 0
    @cy = 0
    @scroll_y = 0
    @scroll_x = 0
  end

  def load_file
    begin
      f = File.open(@path, "r")
      text = f.read
      f.close
      @lines = text.split("\n")
    rescue
      @lines = [""]
    end
    @lines = [""] if @lines.empty?
  end

  def save_file
    begin
      f = File.open(@path, "w")
      # Trailing newline, not just lines joined by one -- POSIX text files
      # end in one, and this app now regularly saves real source files
      # under fsroot/App (the live v2/apps symlink), not just its own
      # scratch notes file: saving without it was confirmed live to strip
      # an existing app.rb's final newline on every single save, which
      # would show up as unwanted diff noise against git history for no
      # reason.
      f.write(@lines.join("\n") + "\n")
      f.close
      @saved_flash = true
    rescue
      @saved_flash = false
    end
  end

  def visible_lines
    (WINDOW_H - TITLE_BAR_H - LINE_H) / LINE_H
  end

  def visible_cols
    (WINDOW_W - TEXT_X) / CHAR_W
  end

  def file_label
    slash = @path.rindex("/")
    slash ? @path[slash + 1, @path.length - slash - 1] : @path
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    draw_status
    draw_gutter
    draw_lines
    draw_cursor
    acid_draw_window_border
  end

  def draw_status
    acid_fill_rect(0, TITLE_BAR_H, WINDOW_W, LINE_H, BG_COLOR)
    left = @saved_flash ? "saved" : file_label
    right = "#{@cy + 1},#{@cx + 1}  #{@lines.length}L"
    acid_draw_text(left[0, 20], 2, TITLE_BAR_H + 1, STATUS_COLOR, BG_COLOR)
    acid_draw_text(right, WINDOW_W - right.length * CHAR_W - 2, TITLE_BAR_H + 1, STATUS_COLOR, BG_COLOR)
  end

  def draw_gutter
    y = TITLE_BAR_H + LINE_H
    acid_fill_rect(0, y, GUTTER_W, WINDOW_H - y, GUTTER_BG)
    i = 0
    while i < visible_lines
      idx = @scroll_y + i
      break if idx >= @lines.length
      num = ( idx + 1 ).to_s
      acid_draw_text(num, GUTTER_W - num.length * CHAR_W - 2, y + i * LINE_H + 1, GUTTER_COLOR, GUTTER_BG)
      i += 1
    end
  end

  def draw_lines
    y = TITLE_BAR_H + LINE_H
    i = 0
    while i < visible_lines
      idx = @scroll_y + i
      text = idx < @lines.length ? @lines[idx] : ""
      acid_fill_rect(TEXT_X, y, WINDOW_W - TEXT_X, LINE_H, BODY_BG)
      visible_text = text[@scroll_x, visible_cols] || ""
      acid_draw_text(visible_text, TEXT_X, y + 1, TEXT_COLOR, BODY_BG)
      y += LINE_H
      i += 1
    end
  end

  def draw_cursor
    row = @cy - @scroll_y
    return if row < 0 || row >= visible_lines
    col = @cx - @scroll_x
    return if col < 0 || col >= visible_cols
    x = TEXT_X + col * CHAR_W
    y = TITLE_BAR_H + LINE_H + row * LINE_H
    acid_fill_rect(x, y + LINE_H - 2, CHAR_W, 2, CURSOR_COLOR)
  end

  def on_key(code, pressed)
    return unless pressed
    @saved_flash = false
    if code == AcidKeys::ESCAPE
      save_file
    elsif code == AcidKeys::UP
      move_cursor(0, -1)
    elsif code == AcidKeys::DOWN
      move_cursor(0, 1)
    elsif code == AcidKeys::LEFT
      move_cursor(-1, 0)
    elsif code == AcidKeys::RIGHT
      move_cursor(1, 0)
    elsif code == AcidKeys::ENTER
      split_line
    elsif code == AcidKeys::BACKSPACE
      backspace
    elsif code >= 32 && code <= 126
      insert_char(code)
    end
    ensure_scroll
    redraw
  end

  def current_line
    @lines[@cy]
  end

  def move_cursor(dx, dy)
    if dy != 0
      @cy += dy
      @cy = 0 if @cy < 0
      @cy = @lines.length - 1 if @cy >= @lines.length
      @cx = current_line.length if @cx > current_line.length
    end
    if dx != 0
      @cx += dx
      if @cx < 0
        if @cy > 0
          @cy -= 1
          @cx = current_line.length
        else
          @cx = 0
        end
      elsif @cx > current_line.length
        if @cy < @lines.length - 1
          @cy += 1
          @cx = 0
        else
          @cx = current_line.length
        end
      end
    end
  end

  def insert_char(code)
    line = current_line
    ch = code.chr
    @lines[@cy] = line[0, @cx] + ch + line[@cx, line.length - @cx]
    @cx += 1
  end

  def split_line
    line = current_line
    before = line[0, @cx]
    after = line[@cx, line.length - @cx]
    @lines[@cy] = before
    @lines.insert(@cy + 1, after)
    @cy += 1
    @cx = 0
  end

  def backspace
    if @cx > 0
      line = current_line
      @lines[@cy] = line[0, @cx - 1] + line[@cx, line.length - @cx]
      @cx -= 1
    elsif @cy > 0
      prev_len = @lines[@cy - 1].length
      @lines[@cy - 1] = @lines[@cy - 1] + @lines[@cy]
      @lines.delete_at(@cy)
      @cy -= 1
      @cx = prev_len
    end
  end

  def ensure_scroll
    if @cy < @scroll_y
      @scroll_y = @cy
    elsif @cy >= @scroll_y + visible_lines
      @scroll_y = @cy - visible_lines + 1
    end
    if @cx < @scroll_x
      @scroll_x = @cx
    elsif @cx >= @scroll_x + visible_cols
      @scroll_x = @cx - visible_cols + 1
    end
  end
end

EditorApp.new.start
