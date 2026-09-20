class EditorApp < AcidApp
  include EditorCmd

  # Must match editor.app.toml and kernel_layout.h's KERNEL_TITLE_BAR_H.
  # 420x280 gives 25 lines of 65 columns; the old 240x170 gave 14 of 35,
  # which is a viewer more than an editor. The screen is 640x360, so two
  # of these still fit side by side.
  WINDOW_W = 420
  WINDOW_H = 280
  TITLE_BAR_H = 16
  LINE_H = 10
  CHAR_W = 6

  # The status line moved to the bottom of the window: command mode (Task
  # 6) raises its strip above it, and a command surface that grows upward
  # from the bottom edge doesn't push the text you're looking at around.
  STATUS_Y = WINDOW_H - LINE_H
  TEXT_Y = TITLE_BAR_H

  GUTTER_CHARS = 4
  GUTTER_W = GUTTER_CHARS * CHAR_W
  TEXT_X = GUTTER_W + 2

  DEFAULT_FILE = "v2/fsroot/Home/notes.txt"

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
    @buf = Buffer.new(read_lines)
    @scroll_y = 0
    @scroll_x = 0
    @message = nil
  end

  def read_lines
    f = File.open(@path, "r")
    text = f.read
    f.close
    text.split("\n")
  rescue
    [""]
  end

  def save_file
    f = File.open(@path, "w")
    # Trailing newline, not just lines joined by one -- POSIX text files
    # end in one, and this app regularly saves real source files under
    # fsroot/App (the live v2/apps symlink): saving without it was
    # confirmed live to strip an existing app.rb's final newline on every
    # save, which is diff noise against git history for no reason.
    f.write(@buf.lines.join("\n") + "\n")
    f.close
    @buf.mark_saved
    @message = "saved"
    true
  rescue
    @message = "save failed"
    false
  end

  def visible_lines
    (STATUS_Y - TEXT_Y) / LINE_H
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
    draw_gutter
    draw_lines
    draw_cursor
    draw_cmd_strip if cmd_active?
    if cmd_prompt_active?
      draw_cmd_prompt
    else
      draw_status
    end
    acid_draw_window_border
  end

  def draw_status
    acid_fill_rect(0, STATUS_Y, WINDOW_W, LINE_H, BG_COLOR)
    left = @message ? @message : (file_label + (@buf.modified? ? " *" : ""))
    right = "#{@buf.cy + 1},#{@buf.cx + 1}  #{@buf.line_count}L"
    acid_draw_text(left[0, 28], 2, STATUS_Y + 1, STATUS_COLOR, BG_COLOR)
    acid_draw_text(right, WINDOW_W - right.length * CHAR_W - 2, STATUS_Y + 1,
                   STATUS_COLOR, BG_COLOR)
  end

  def draw_gutter
    acid_fill_rect(0, TEXT_Y, GUTTER_W, STATUS_Y - TEXT_Y, GUTTER_BG)
    i = 0
    while i < visible_lines
      idx = @scroll_y + i
      break if idx >= @buf.line_count
      num = (idx + 1).to_s
      acid_draw_text(num, GUTTER_W - num.length * CHAR_W - 2,
                     TEXT_Y + i * LINE_H + 1, GUTTER_COLOR, GUTTER_BG)
      i += 1
    end
  end

  def draw_lines
    i = 0
    while i < visible_lines
      idx = @scroll_y + i
      y = TEXT_Y + i * LINE_H
      acid_fill_rect(TEXT_X, y, WINDOW_W - TEXT_X, LINE_H, BODY_BG)
      if idx < @buf.line_count
        text = @buf.line(idx)
        visible_text = text[@scroll_x, visible_cols] || ""
        acid_draw_text(visible_text, TEXT_X, y + 1, TEXT_COLOR, BODY_BG)
      end
      i += 1
    end
  end

  def draw_cursor
    row = @buf.cy - @scroll_y
    return if row < 0 || row >= visible_lines
    col = @buf.cx - @scroll_x
    return if col < 0 || col >= visible_cols
    x = TEXT_X + col * CHAR_W
    y = TEXT_Y + row * LINE_H
    acid_fill_rect(x, y + LINE_H - 2, CHAR_W, 2, CURSOR_COLOR)
  end

  def on_key(code, pressed)
    return unless pressed
    return redraw if cmd_prompt_key(code)
    return redraw if cmd_key(code)
    @message = nil
    @quit_armed = false
    if code == AcidKeys::ESCAPE
      cmd_open
      return redraw
    elsif code == AcidKeys::UP
      @buf.move(0, -1)
    elsif code == AcidKeys::DOWN
      @buf.move(0, 1)
    elsif code == AcidKeys::LEFT
      @buf.move(-1, 0)
    elsif code == AcidKeys::RIGHT
      @buf.move(1, 0)
    elsif code == AcidKeys::ENTER
      @buf.split_line
    elsif code == AcidKeys::BACKSPACE
      @buf.backspace
    elsif code == AcidKeys::DELETE
      @buf.delete_forward
    elsif code == AcidKeys::TAB
      # Two spaces, not a tab character: every width calculation in this
      # app counts characters, and a literal tab would make the cursor
      # column and the drawn column disagree from that point on.
      @buf.insert_text("  ")
    elsif code >= 32 && code <= 126
      @buf.insert_char(code.chr)
    end
    ensure_scroll
    redraw
  end

  def ensure_scroll
    if @buf.cy < @scroll_y
      @scroll_y = @buf.cy
    elsif @buf.cy >= @scroll_y + visible_lines
      @scroll_y = @buf.cy - visible_lines + 1
    end
    if @buf.cx < @scroll_x
      @scroll_x = @buf.cx
    elsif @buf.cx >= @scroll_x + visible_cols
      @scroll_x = @buf.cx - visible_cols + 1
    end
  end
end

EditorApp.new.start
