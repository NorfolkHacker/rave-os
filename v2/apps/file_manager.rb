class FileManagerApp < AcidApp
  # Must match the kernel_spawn_app(...) call that spawns this app (Task 7)
  # and kernel_layout.h's KERNEL_TITLE_BAR_H.
  WINDOW_W = 220
  WINDOW_H = 160
  TITLE_BAR_H = 16
  ROW_H = 12
  ROOT_DIR = "v2/fsroot"

  BG_COLOR = 0x0B1712      # THEME_PANEL -- header row
  BODY_BG = 0x050607       # THEME_BG -- list/preview rows
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  DIR_COLOR = 0x00FF66     # THEME_HARD -- accent for directory entries
  TOML_COLOR = 0xB026FF    # THEME_VIOLET -- .app.toml manifests, i.e. the
                           # entries that actually launch something when
                           # clicked (activate_selected below), as opposed
                           # to the .rb beside them, which only opens in
                           # the editor
  SEL_BG = 0x123322        # THEME_PANEL's documented button-hover shade,
                           # reused for the selected-row highlight

  def on_create
    @dir = ROOT_DIR
    @entries = []
    @selected = 0
    @scroll = 0
    @preview = nil       # nil = browsing; a String = previewing this
                          # file's content
    @preview_name = nil
    @preview_scroll = 0
    scan_dir
  end

  def scan_dir
    @entries = []
    @scroll = 0
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

  # How many entry rows the listing has room for, below its own one-row
  # path header -- the number draw_listing/on_touch/scrolling all need to
  # agree on, previously duplicated as a bare `visible_rows - 1` in each.
  def visible_listing_rows
    visible_rows - 1
  end

  # Keeps @selected on screen by moving @scroll to match, same idea as
  # editor.rb's own ensure_scroll -- without this, a directory with more
  # entries than fit on screen (v2/apps, now browsable via fsroot/App,
  # easily has more files than this window's dozen or so visible rows)
  # left every entry past the first screenful permanently unreachable:
  # arrow-key selection moved @selected past the visible range with
  # nothing on screen ever scrolling to show it, and a tap below the
  # visible rows had nothing real to hit-test against anyway.
  def ensure_listing_scroll
    if @selected < @scroll
      @scroll = @selected
    elsif @selected >= @scroll + visible_listing_rows
      @scroll = @selected - visible_listing_rows + 1
    end
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    if @preview
      draw_preview
    else
      draw_listing
    end
    acid_draw_window_border
  end

  def draw_listing
    y = TITLE_BAR_H
    acid_fill_rect(0, y, WINDOW_W, ROW_H, BG_COLOR)
    label = @entries.length > visible_listing_rows ? "#{@dir} (#{@selected + 1}/#{@entries.length})" : @dir
    acid_draw_text(label, 2, y + 2, TEXT_COLOR, BG_COLOR)
    y += ROW_H
    i = @scroll
    while i < @entries.length && i < @scroll + visible_listing_rows
      e = @entries[i]
      row_bg = (i == @selected) ? SEL_BG : BODY_BG
      acid_fill_rect(0, y, WINDOW_W, ROW_H, row_bg)
      entry_label = e[:dir] ? "[#{e[:name]}]" : " #{e[:name]} (#{e[:size]}B)"
      color = entry_color(e)
      acid_draw_text(entry_label[0, 34], 2, y + 2, color, row_bg)
      y += ROW_H
      i += 1
    end
  end

  # Directories green, launchable .toml manifests violet, everything else
  # plain text -- so a glance at v2/apps tells you which half of each
  # <name>.rb / <name>.app.toml pair is the one that starts the app.
  def entry_color(e)
    return DIR_COLOR if e[:dir]
    return TOML_COLOR if e[:name].end_with?(".toml")
    TEXT_COLOR
  end

  def draw_preview
    lines = @preview.split("\n")
    acid_fill_rect(0, TITLE_BAR_H, WINDOW_W, ROW_H, BG_COLOR)
    header = lines.length > visible_listing_rows ? "#{@preview_name} (#{@preview_scroll + 1}/#{lines.length})" : @preview_name
    acid_draw_text(header, 2, TITLE_BAR_H + 2, TEXT_COLOR, BG_COLOR)
    y = TITLE_BAR_H + ROW_H
    i = @preview_scroll
    while i < lines.length && i < @preview_scroll + visible_listing_rows
      acid_fill_rect(0, y, WINDOW_W, ROW_H, BODY_BG)
      acid_draw_text(lines[i][0, 34], 2, y + 2, TEXT_COLOR, BODY_BG)
      y += ROW_H
      i += 1
    end
  end

  def max_preview_scroll(lines)
    over = lines.length - visible_listing_rows
    over > 0 ? over : 0
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
    row = (y - TITLE_BAR_H) / ROW_H - 1 + @scroll
    return if row < 0 || row >= @entries.length
    @selected = row
    activate_selected
  end

  def on_key(code, pressed)
    return unless pressed
    if @preview
      lines = @preview.split("\n")
      if code == AcidKeys::ESCAPE
        @preview = nil
      elsif code == AcidKeys::UP
        @preview_scroll -= 1 if @preview_scroll > 0
      elsif code == AcidKeys::DOWN
        @preview_scroll += 1 if @preview_scroll < max_preview_scroll(lines)
      end
      redraw
      return
    end
    if code == AcidKeys::UP
      @selected -= 1 if @selected > 0
      ensure_listing_scroll
      redraw
    elsif code == AcidKeys::DOWN
      @selected += 1 if @selected < @entries.length - 1
      ensure_listing_scroll
      redraw
    elsif code == AcidKeys::ENTER
      activate_selected
    elsif code == AcidKeys::BACKSPACE
      go_up
    end
  end

  # Editor's own window size (editor.app.toml) -- kept in sync by comment,
  # the same convention this codebase already uses for other cross-file
  # constants (e.g. desktop.rb's SCREEN_W).
  EDITOR_PATH = "v2/apps/editor.rb"
  EDITOR_W = 420
  EDITOR_H = 280

  def activate_selected
    entry = @entries[@selected]
    return unless entry
    if entry[:name] == ".."
      go_up
    elsif entry[:dir]
      @dir = "#{@dir}/#{entry[:name]}"
      scan_dir
      redraw
    elsif entry[:name].end_with?(".app.toml")
      launch_manifest(entry[:name])
    elsif entry[:name].end_with?(".rb")
      acid_spawn_app(EDITOR_PATH, EDITOR_W, EDITOR_H, "#{@dir}/#{entry[:name]}")
    else
      open_preview(entry[:name])
    end
  end

  # Clicking an app's manifest launches it directly -- games/Piano/etc
  # are deliberately left out of desktop.rb's Menu dropdown now (they opt
  # out via `menu = false` in their own .app.toml) specifically so this
  # is how you reach them: click their icon here instead. Uses the same
  # plain "key = value" manifest format desktop.rb's own parser reads;
  # duplicated rather than shared since the two apps want different
  # things from a manifest (desktop.rb also needs the `menu` flag and
  # registers into the launcher list, this just needs enough to spawn
  # once).
  def launch_manifest(name)
    path = "#{@dir}/#{name}"
    fields = {}
    begin
      f = File.open(path, "r")
      text = f.read
      f.close
      text.split("\n").each do |line|
        line = line.strip
        next if line.empty? || line.start_with?("#")
        eq = line.index("=")
        next unless eq
        key = line[0, eq].strip
        value = line[eq + 1, line.length - eq - 1].strip
        fields[key] = value
      end
    rescue
      return
    end
    return unless fields["w"] && fields["h"]
    rb_path = "#{@dir}/#{name[0, name.length - ".app.toml".length]}.rb"
    # canonical_app_path is defined on AcidApp (v2/apps/lib/acid_app.rb),
    # not here -- browsing to a manifest under fsroot/App (which this app
    # itself makes possible) built rb_path still under fsroot/App, and
    # the launcher registry only matches the canonical v2/apps form (see
    # AcidApp's comment on why). A bare call here resolves through self's
    # actual ancestor chain at runtime (FileManagerApp < AcidApp), the
    # same way cmdbar.rb's bare `quit!` call already relies on AcidApp
    # without EditorCmd redefining it -- no shared module needed for a
    # method the way OWN_SOURCE_SUFFIXES needed one for a constant.
    acid_spawn_app(canonical_app_path(rb_path), fields["w"].to_i, fields["h"].to_i, "")
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
    @preview_scroll = 0
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
