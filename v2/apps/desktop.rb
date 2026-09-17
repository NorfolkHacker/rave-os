class DesktopApp < AcidApp
  # Must match the kernel_spawn_app(...) script_path that spawns this app
  # (sim_main.c / app_main.c) -- app_name is literally that path
  # (kernel_spawn.c registers it verbatim), which is how the taskbar
  # recognizes and skips its own window.
  MY_APP_NAME = "v2/apps/desktop.rb"

  BUTTON_W = 60
  BUTTON_H = 18
  BUTTON_MARGIN_X = 2
  BUTTON_MARGIN_Y = 2

  BG_COLOR = 0x0B1712      # THEME_PANEL
  ACCENT_COLOR = 0x00FF66  # THEME_HARD
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  TEXT_DARK = 0x050607     # THEME_BG -- used as the label color on an
                           # accent-filled focused button, for contrast
                           # (mirrors docs/BUILD_LOG.md's documented
                           # "pressed state inverts to a solid --hard
                           # fill, label switches to a dark color" rule).

  # No on_create override needed: AcidApp#start already calls redraw once,
  # automatically, right after on_create -- DesktopApp has no other setup
  # to do (active_windows is computed fresh on every redraw, not cached),
  # matching file_manager.rb/editor.rb/acid_blaster.rb's own convention of
  # never calling redraw from inside on_create itself.

  def on_idle
    # Nothing has to touch the desktop strip itself for the taskbar to go
    # stale -- clicking directly from one app window to another is the
    # common case. Checking on every idle timeout (roughly 5Hz, the
    # existing 200ms acid_poll_event timeout) keeps the focus highlight
    # and window list live without a dedicated notification channel --
    # but only actually REDRAW when something's different from last time
    # (@last_state below), otherwise this unconditionally clears and
    # repaints the whole strip 5 times a second with identical content,
    # which is a real, continuous flicker for something that's visually a
    # no-op almost all of the time.
    redraw_if_changed
  end

  def redraw
    acid_draw_desktop_strip
    active_windows.each_with_index do |entry, slot|
      draw_button(slot, entry[1])
    end
  end

  # Same as redraw, but a no-op when the window list and focus state are
  # identical to last time -- see on_idle's comment for why this matters
  # (a plain redraw here would flicker the whole strip 5 times a second
  # for no visible change almost all of the time).
  def redraw_if_changed
    windows = active_windows
    sig = state_signature(windows)
    return if sig == @last_state
    @last_state = sig
    acid_draw_desktop_strip
    windows.each_with_index do |entry, slot|
      draw_button(slot, entry[1])
    end
  end

  def on_touch(x, y, pressed)
    return unless pressed
    slot = x / BUTTON_W
    entry = active_windows[slot]
    return unless entry
    acid_activate_window(entry[0])
    redraw_if_changed
  end

  private

  # A plain value (Array of Arrays, structurally comparable with ==) that
  # changes if and only if what the strip would actually LOOK like
  # changes: which apps are open, in which slots, and which one is
  # focused. Position/size aren't included -- the taskbar never draws
  # those -- so a window being dragged around the screen doesn't cause
  # the strip to think it needs a redraw every tick.
  def state_signature(windows)
    windows.map { |entry| [entry[1][0], entry[1][5]] }
  end

  # [[kernel_index, info], ...] for every in-use window except this one.
  # Recomputed on every call rather than cached -- at most 8 entries, and
  # avoids any staleness between what's drawn and what a tap acts on.
  def active_windows
    list = []
    i = 0
    max = acid_window_max
    while i < max
      info = acid_window_info(i)
      list << [i, info] if info && info[0] != MY_APP_NAME
      i += 1
    end
    list
  end

  def draw_button(slot, info)
    name = info[0]
    focused = info[5]
    x = slot * BUTTON_W + BUTTON_MARGIN_X
    y = BUTTON_MARGIN_Y
    w = BUTTON_W - BUTTON_MARGIN_X * 2
    h = BUTTON_H
    bg = focused ? ACCENT_COLOR : BG_COLOR
    fg = focused ? TEXT_DARK : TEXT_COLOR
    acid_fill_rect(x, y, w, h, bg)
    acid_draw_text(short_name(name), x + 3, y + 5, fg, bg)
  end

  # app_name is a full script path (e.g. "v2/apps/acid_blaster.rb") -- show
  # just the filename, stripped of directory and extension, truncated to
  # fit the button.
  def short_name(path)
    slash = path.rindex("/")
    base = slash ? path[slash + 1, path.length - slash - 1] : path
    dot = base.rindex(".")
    base = base[0, dot] if dot
    base = base[0, 8] if base.length > 8
    base
  end
end

DesktopApp.new.start
