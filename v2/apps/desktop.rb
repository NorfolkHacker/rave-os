class DesktopApp < AcidApp
  # Must match the kernel_spawn_app(...) script_path that spawns this app
  # (sim_main.c / app_main.c) -- app_name is literally that path
  # (kernel_spawn.c registers it verbatim), which is how the taskbar
  # recognizes and skips its own window.
  MY_APP_NAME = "v2/apps/desktop.rb"

  # Must match app_main.c's own kernel_spawn_app(MY_APP_NAME, 0, 0, 320, 24, 0)
  # call -- there's no way to ask the kernel for "my own window's width" from
  # here (acid_window_info would work, but only once this app has actually
  # registered itself, which every app_main.c app spawns before the router
  # task -- boot ordering makes this safe today, but hardcoding what's
  # already hardcoded one file over is simpler and just as correct).
  SCREEN_W = 320

  BUTTON_W = 60
  BUTTON_H = 18
  BUTTON_MARGIN_X = 2
  BUTTON_MARGIN_Y = 2

  # Always the rightmost slot, in both modes -- Menu to enter the launcher,
  # Back to leave it -- so there's one consistent place to tap regardless of
  # what's currently on screen. Reserving a whole slot for it means it can
  # never collide with a real window/app button as long as there are at most
  # four of those (SCREEN_W / BUTTON_W - 1) -- the same slot-count ceiling
  # the taskbar already had before this existed, not a new limitation.
  MENU_SLOT_X = SCREEN_W - BUTTON_W

  # How many window/app buttons actually fit before they'd start drawing
  # underneath the reserved menu slot. Windows beyond this many are simply
  # not listed in the taskbar (the window itself is still open and usable,
  # just not represented here) -- a small-screen limitation that predates
  # the launcher, just newly reachable now that opening a 5th app is
  # actually possible.
  MAX_TASKBAR_SLOTS = MENU_SLOT_X / BUTTON_W

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
  # never calling redraw from inside on_create itself. @mode starts as nil,
  # treated the same as :windows everywhere it's checked.

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
    # no-op almost all of the time. The launcher view never goes stale on
    # its own (it's a fixed table, not live window state), so it never
    # needs this at all.
    return if @mode == :launcher
    redraw_if_changed
  end

  def redraw
    acid_draw_desktop_strip
    if @mode == :launcher
      draw_launcher_buttons
    else
      active_windows.first(MAX_TASKBAR_SLOTS).each_with_index do |entry, slot|
        draw_button(slot, entry[1])
      end
    end
    draw_menu_button
  end

  # Same as redraw, but a no-op when the window list and focus state are
  # identical to last time -- see on_idle's comment for why this matters
  # (a plain redraw here would flicker the whole strip 5 times a second
  # for no visible change almost all of the time). Only ever called while
  # in :windows mode (on_idle guards the launcher case, and on_touch only
  # calls this from the window-activate path).
  def redraw_if_changed
    windows = active_windows
    sig = state_signature(windows)
    return if sig == @last_state
    @last_state = sig
    acid_draw_desktop_strip
    windows.first(MAX_TASKBAR_SLOTS).each_with_index do |entry, slot|
      draw_button(slot, entry[1])
    end
    draw_menu_button
  end

  def on_touch(x, y, pressed)
    return unless pressed
    if in_menu_slot?(x)
      @mode = ( @mode == :launcher ) ? :windows : :launcher
      redraw
      return
    end
    if @mode == :launcher
      slot = x / BUTTON_W
      if slot < MAX_TASKBAR_SLOTS && slot < acid_launcher_count
        acid_launcher_spawn(slot)
      end
      @mode = :windows
      redraw
      return
    end
    slot = x / BUTTON_W
    return if slot >= MAX_TASKBAR_SLOTS
    entry = active_windows[slot]
    return unless entry
    acid_activate_window(entry[0])
    redraw_if_changed
  end

  private

  def in_menu_slot?(x)
    x >= MENU_SLOT_X
  end

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
    draw_slot(slot, short_name(name), focused ? ACCENT_COLOR : BG_COLOR,
              focused ? TEXT_DARK : TEXT_COLOR)
  end

  def draw_launcher_buttons
    count = acid_launcher_count
    i = 0
    while i < count
      draw_slot(i, short_name(acid_launcher_path(i)), BG_COLOR, TEXT_COLOR)
      i += 1
    end
  end

  # Always the same reserved rightmost slot (MENU_SLOT_X) -- highlighted
  # like a focused window button while in the launcher, as an "you are
  # here, tap to go back" cue; plain otherwise.
  def draw_menu_button
    in_launcher = ( @mode == :launcher )
    label = in_launcher ? "Back" : "Menu"
    bg = in_launcher ? ACCENT_COLOR : BG_COLOR
    fg = in_launcher ? TEXT_DARK : TEXT_COLOR
    x = MENU_SLOT_X + BUTTON_MARGIN_X
    y = BUTTON_MARGIN_Y
    w = BUTTON_W - BUTTON_MARGIN_X * 2
    h = BUTTON_H
    acid_fill_rect(x, y, w, h, bg)
    acid_draw_text(label, x + 3, y + 5, fg, bg)
  end

  def draw_slot(slot, label, bg, fg)
    x = slot * BUTTON_W + BUTTON_MARGIN_X
    y = BUTTON_MARGIN_Y
    w = BUTTON_W - BUTTON_MARGIN_X * 2
    h = BUTTON_H
    acid_fill_rect(x, y, w, h, bg)
    acid_draw_text(label[0, 8], x + 3, y + 5, fg, bg)
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
