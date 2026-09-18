class EditorApp < AcidApp
  # Must match the kernel_spawn_app(...) call that spawns this app (Task 7)
  # and kernel_layout.h's KERNEL_TITLE_BAR_H.
  WINDOW_W = 240
  WINDOW_H = 170
  TITLE_BAR_H = 16
  LINE_H = 10
  EDIT_FILE = "v2/fsroot/Home/notes.txt"

  BG_COLOR = 0x0B1712      # THEME_PANEL -- status line
  BODY_BG = 0x050607       # THEME_BG -- text body
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  CURSOR_COLOR = 0x00FF66  # THEME_HARD
  STATUS_COLOR = 0x9DAAA3  # THEME_MUTED

  def on_create
    load_file
    @cx = 0
    @cy = 0
    @scroll_y = 0
    @status = "ESC=save  #{@lines.length} lines"
  end

  def load_file
    begin
      f = File.open(EDIT_FILE, "r")
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
      f = File.open(EDIT_FILE, "w")
      f.write(@lines.join("\n"))
      f.close
      @status = "saved (#{@lines.length} lines)"
    rescue => e
      @status = "save failed: #{e.message}"
    end
  end

  def visible_lines
    (WINDOW_H - TITLE_BAR_H - LINE_H) / LINE_H
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    draw_status
    draw_lines
    draw_cursor
    acid_draw_window_border
  end

  def draw_status
    acid_fill_rect(0, TITLE_BAR_H, WINDOW_W, LINE_H, BG_COLOR)
    acid_draw_text(@status, 2, TITLE_BAR_H + 1, STATUS_COLOR, BG_COLOR)
  end

  def draw_lines
    y = TITLE_BAR_H + LINE_H
    i = 0
    while i < visible_lines
      idx = @scroll_y + i
      text = idx < @lines.length ? @lines[idx] : ""
      acid_fill_rect(0, y, WINDOW_W, LINE_H, BODY_BG)
      acid_draw_text(text[0, 38], 2, y + 1, TEXT_COLOR, BODY_BG)
      y += LINE_H
      i += 1
    end
  end

  def draw_cursor
    row = @cy - @scroll_y
    return if row < 0 || row >= visible_lines
    x = 2 + @cx * 6
    y = TITLE_BAR_H + LINE_H + row * LINE_H
    acid_fill_rect(x, y + LINE_H - 2, 6, 2, CURSOR_COLOR)
  end

  def on_key(code, pressed)
    return unless pressed
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
  end
end

EditorApp.new.start
