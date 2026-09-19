# App Store -- browse every registered app (not just what's in the desktop
# Menu dropdown) with its real description, tap a row to launch it, tap
# the tag on the right to toggle whether it shows in that dropdown. There
# is no download/install here (there is nowhere else for an app to come
# from -- every app already lives in v2/apps) -- what this actually adds
# over File Manager's raw file browsing is the semantic view: names,
# descriptions, and a structured Menu-visibility toggle instead of editing
# a manifest's text by hand.
#
# The toggle writes straight to that app's own .app.toml (same file
# desktop.rb's own launcher scan reads at boot) -- it takes effect the
# NEXT time desktop.rb scans (i.e. next boot), not live, since desktop.rb
# only builds @menu_visible once at its own on_create. Documented, not
# silently wrong: reflecting a running desktop's dropdown live would mean
# either polling every app's manifest file every frame or a new cross-app
# notification path, real scope beyond what this app is for.
class AppStoreApp < AcidApp
  WINDOW_W = 220
  WINDOW_H = 180
  TITLE_BAR_H = 16
  HEADER_H = 12
  ROW_H = 20

  BG_COLOR = 0x050607      # THEME_BG
  HEADER_BG = 0x0B1712     # THEME_PANEL
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  MUTED_COLOR = 0x9DAAA3   # THEME_MUTED
  HARD_COLOR = 0x00FF66    # THEME_HARD
  PANEL_COLOR = 0x0B1712   # THEME_PANEL

  TAG_W = 44

  def on_create
    @scroll = 0
    @touch_held = false
    load_apps
  end

  def window_title
    "App Store"
  end

  # One entry per acid_launcher_* registry slot (the same list desktop.rb
  # itself built at boot from every v2/apps/*.app.toml) -- this app reads
  # each one's OWN manifest again just for the fields the registry doesn't
  # carry (desc, menu), the same "read the manifest a second time for
  # different fields" tradeoff file_manager.rb's launch_manifest already
  # documents making, rather than a shared parser neither app would fully
  # need.
  def load_apps
    @apps = []
    count = acid_launcher_count
    i = 0
    while i < count
      rb_path = acid_launcher_path(i)
      name = acid_launcher_name(i)
      toml_path = rb_path[0, rb_path.length - 3] + ".app.toml"
      fields = parse_manifest(toml_path)
      @apps << {
        index: i,
        name: name,
        toml_path: toml_path,
        desc: fields["desc"] || "",
        visible: fields["menu"] != "false"
      }
      i += 1
    end
  end

  def parse_manifest(path)
    fields = {}
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
    fields
  rescue
    {}
  end

  def visible_rows
    (WINDOW_H - TITLE_BAR_H - HEADER_H) / ROW_H
  end

  def max_scroll
    over = @apps.length - visible_rows
    over > 0 ? over : 0
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)

    y = TITLE_BAR_H
    acid_fill_rect(0, y, WINDOW_W, HEADER_H, HEADER_BG)
    top = @scroll + 1
    bottom = [ @scroll + visible_rows, @apps.length ].min
    acid_draw_text("APPS (#{top}-#{bottom}/#{@apps.length})", 2, y + 2, MUTED_COLOR, HEADER_BG)
    y += HEADER_H

    i = @scroll
    while i < @apps.length && i < @scroll + visible_rows
      draw_row(@apps[i], y)
      y += ROW_H
      i += 1
    end

    acid_draw_window_border
  end

  def draw_row(app, y)
    acid_fill_rect(0, y, WINDOW_W, ROW_H, BG_COLOR)
    acid_draw_text(app[:name][0, 20], 2, y + 1, TEXT_COLOR, BG_COLOR)
    acid_draw_text(app[:desc][0, 24], 2, y + 10, MUTED_COLOR, BG_COLOR)
    draw_tag(app, y)
  end

  def draw_tag(app, y)
    label = app[:visible] ? "MENU" : "HIDDEN"
    fg = app[:visible] ? 0x050607 : MUTED_COLOR
    bg = app[:visible] ? HARD_COLOR : PANEL_COLOR
    x = WINDOW_W - TAG_W - 2
    acid_fill_rect(x, y + 2, TAG_W, ROW_H - 4, bg)
    acid_draw_text(label, x + 2, y + ROW_H / 2 - 6, fg, bg)
  end

  def on_touch(x, y, pressed)
    # Same one-press-per-hold discipline as every other app in this OS
    # with a tap-to-act row (file_manager.rb, sysmon.rb) -- see either of
    # their own comments for why: the router resends TOUCH on every ~16ms
    # tick while held, and without this guard a single tap-and-hold would
    # fire the launch (or the toggle) many times over.
    unless pressed
      @touch_held = false
      return
    end
    return if @touch_held
    @touch_held = true

    header_bottom = TITLE_BAR_H + HEADER_H
    return if y < header_bottom
    row = (y - header_bottom) / ROW_H + @scroll
    return if row < 0 || row >= @apps.length
    app = @apps[row]

    if x >= WINDOW_W - TAG_W - 2
      toggle_menu(app)
    else
      acid_launcher_spawn(app[:index])
    end
  end

  def toggle_menu(app)
    write_menu_field(app[:toml_path], !app[:visible])
    app[:visible] = !app[:visible]
    redraw
  end

  # Rewrites just the `menu = ...` line (adding or dropping it as needed)
  # and leaves every other line untouched. Mirrors editor.rb's own
  # save_file fix: `+ "\n"` so this never silently strips the manifest's
  # trailing newline the way a bare join once did there.
  def write_menu_field(path, want_visible)
    f = File.open(path, "r")
    text = f.read
    f.close
    lines = text.split("\n").reject do |line|
      stripped = line.strip
      eq = stripped.index("=")
      eq && stripped[0, eq].strip == "menu"
    end
    lines << "menu = false" unless want_visible
    f = File.open(path, "w")
    f.write(lines.join("\n") + "\n")
    f.close
  rescue
    # A manifest that can't be read/written back shouldn't crash the
    # whole app -- the in-memory @apps entry still flips so the running
    # session stays consistent, it just won't survive a restart.
  end

  def on_key(code, pressed)
    return unless pressed
    if code == AcidKeys::UP
      @scroll -= 1 if @scroll > 0
      redraw
    elsif code == AcidKeys::DOWN
      @scroll += 1 if @scroll < max_scroll
      redraw
    end
  end
end

AppStoreApp.new.start
