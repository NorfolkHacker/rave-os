class FileManagerApp < AcidApp
  # Must match the kernel_spawn_app(...) call that spawns this app (Task 7)
  # and kernel_layout.h's KERNEL_TITLE_BAR_H.
  WINDOW_W = 220
  WINDOW_H = 160
  TITLE_BAR_H = 16
  ROW_H = 12
  ROOT_DIR = "v2/home"

  BG_COLOR = 0x0B1712      # THEME_PANEL -- header row
  BODY_BG = 0x050607       # THEME_BG -- list/preview rows
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  DIR_COLOR = 0x00FF66     # THEME_HARD -- accent for directory entries
  SEL_BG = 0x123322        # THEME_PANEL's documented button-hover shade,
                           # reused for the selected-row highlight

  def on_create
    @dir = ROOT_DIR
    @entries = []
    @selected = 0
    @preview = nil       # nil = browsing; a String = previewing this
                          # file's content
    @preview_name = nil
    scan_dir
  end

  def scan_dir
    @entries = []
    @entries << { name: "..", dir: true, size: 0 } unless @dir == ROOT_DIR
    begin
      d = Dir.open(@dir)
      names = []
      while (ent = d.read)
        names << ent unless ent == "." || ent == ".."
      end
      d.close
      names.sort.each do |name|
        path = "#{@dir}/#{name}"
        is_dir = false
        size = 0
        begin
          sub = Dir.open(path)
          sub.close
          is_dir = true
        rescue
          begin
            size = File.size(path)
          rescue
            size = 0
          end
        end
        @entries << { name: name, dir: is_dir, size: size }
      end
    rescue => e
      @entries << { name: "(error: #{e.message})", dir: false, size: 0 }
    end
    @selected = 0
  end

  def visible_rows
    (WINDOW_H - TITLE_BAR_H) / ROW_H
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    if @preview
      draw_preview
    else
      draw_listing
    end
  end

  def draw_listing
    y = TITLE_BAR_H
    acid_fill_rect(0, y, WINDOW_W, ROW_H, BG_COLOR)
    acid_draw_text(@dir, 2, y + 2, TEXT_COLOR, BG_COLOR)
    y += ROW_H
    i = 0
    while i < @entries.length && i < visible_rows - 1
      e = @entries[i]
      row_bg = (i == @selected) ? SEL_BG : BODY_BG
      acid_fill_rect(0, y, WINDOW_W, ROW_H, row_bg)
      label = e[:dir] ? "[#{e[:name]}]" : " #{e[:name]} (#{e[:size]}B)"
      color = e[:dir] ? DIR_COLOR : TEXT_COLOR
      acid_draw_text(label[0, 34], 2, y + 2, color, row_bg)
      y += ROW_H
      i += 1
    end
  end

  def draw_preview
    acid_fill_rect(0, TITLE_BAR_H, WINDOW_W, ROW_H, BG_COLOR)
    acid_draw_text(@preview_name, 2, TITLE_BAR_H + 2, TEXT_COLOR, BG_COLOR)
    lines = @preview.split("\n")
    y = TITLE_BAR_H + ROW_H
    i = 0
    while i < lines.length && i < visible_rows - 1
      acid_fill_rect(0, y, WINDOW_W, ROW_H, BODY_BG)
      acid_draw_text(lines[i][0, 34], 2, y + 2, TEXT_COLOR, BODY_BG)
      y += ROW_H
      i += 1
    end
  end

  def on_touch(x, y, pressed)
    # The router sends a TOUCH event on every ~16ms tick for as long as
    # the mouse stays held, not just once on the initial press (demo_touch
    # relies on exactly that, to draw a continuous trail while dragging).
    # Without this guard, holding down on a row re-ran activate_selected
    # -- reopening the file and redrawing -- on every single one of those
    # ticks, which looked like the file rapidly opening and closing
    # (reported live as "intense flicker" while holding a row). A plain
    # click should select/open once, not repeatedly for as long as it's
    # held.
    unless pressed
      @touch_held = false
      return
    end
    return if @touch_held
    @touch_held = true
    if @preview
      @preview = nil
      redraw
      return
    end
    row = (y - TITLE_BAR_H) / ROW_H - 1
    return if row < 0 || row >= @entries.length
    @selected = row
    activate_selected
  end

  def on_key(code, pressed)
    return unless pressed
    if @preview
      if code == AcidKeys::ESCAPE
        @preview = nil
        redraw
      end
      return
    end
    if code == AcidKeys::UP
      @selected -= 1 if @selected > 0
      redraw
    elsif code == AcidKeys::DOWN
      @selected += 1 if @selected < @entries.length - 1
      redraw
    elsif code == AcidKeys::ENTER
      activate_selected
    elsif code == AcidKeys::BACKSPACE
      go_up
    end
  end

  def activate_selected
    entry = @entries[@selected]
    return unless entry
    if entry[:name] == ".."
      go_up
    elsif entry[:dir]
      @dir = "#{@dir}/#{entry[:name]}"
      scan_dir
      redraw
    else
      open_preview(entry[:name])
    end
  end

  def go_up
    return if @dir == ROOT_DIR
    slash = @dir.rindex("/")
    @dir = slash ? @dir[0, slash] : ROOT_DIR
    scan_dir
    redraw
  end

  def open_preview(name)
    path = "#{@dir}/#{name}"
    begin
      f = File.open(path, "r")
      @preview = f.read
      f.close
      @preview_name = name
    rescue => e
      @preview = "(cannot open: #{e.message})"
      @preview_name = name
    end
    redraw
  end
end

FileManagerApp.new.start
