class DesktopApp < AcidApp
  # Must match the kernel_spawn_app(...) script_path that spawns this app
  # (sim_main.c / app_main.c) -- app_name is literally that path
  # (kernel_spawn.c registers it verbatim), which is how the taskbar
  # recognizes and skips its own window.
  MY_APP_NAME = "v2/apps/desktop.rb"

  # Must match sim_main.c/app_main.c's own screen width (both hardcode 320
  # directly in their kernel_spawn_app(MY_APP_NAME, 0, 0, 320, ...) call) --
  # there's no shared header between Ruby and those C files to pull this
  # from, so it's kept in sync by comment, the same way MY_APP_NAME already
  # has to be.
  SCREEN_W = 320

  # The visible taskbar strip's height -- matches kernel_layout.h's
  # KERNEL_DESKTOP_STRIP_H (24), which is what the router uses to decide a
  # touch belongs to the desktop strip unconditionally, bypassing normal
  # window z-order hit-testing. Below this row, hit-testing is normal, and
  # desktop only wins it by being registered taller than this (see
  # TOTAL_H) and briefly raised to the front while its dropdown is open.
  STRIP_H = 24

  BUTTON_W = 60
  BUTTON_H = 18
  BUTTON_MARGIN_X = 2
  BUTTON_MARGIN_Y = 2

  # Always the rightmost slot in the strip -- Menu to open the dropdown,
  # Back to close it -- so there's one consistent place to tap regardless
  # of what's currently on screen. Reserving a whole slot for it means it
  # can never collide with a real window button as long as there are at
  # most four of those (SCREEN_W / BUTTON_W - 1) -- the same slot-count
  # ceiling the taskbar already had before this existed, not a new
  # limitation.
  MENU_SLOT_X = SCREEN_W - BUTTON_W

  # How many window buttons actually fit in the strip before they'd start
  # drawing underneath the reserved menu slot. Windows beyond this many are
  # simply not listed in the taskbar (the window itself is still open and
  # usable, just not represented here) -- a small-screen limitation that
  # predates the launcher, just newly reachable now that opening a 5th app
  # is actually possible. This is a horizontal-slot-width constraint
  # specific to the taskbar row (BUTTON_W columns) -- NOT the dropdown's
  # own capacity (see MAX_LAUNCHER_ITEMS below), which has no such
  # constraint since dropdown rows are full-width, not columns.
  MAX_TASKBAR_SLOTS = MENU_SLOT_X / BUTTON_W

  # The dropdown itself: a full-width panel directly under the strip,
  # one row per launchable app. Deliberately its own constant, not
  # MAX_TASKBAR_SLOTS -- a dropdown row spans the full screen width, so
  # it isn't limited by how many BUTTON_W-wide columns fit in the strip
  # the way MAX_TASKBAR_SLOTS is; the only real constraint is vertical
  # room on a 240px-tall screen, which 6 rows comfortably leaves (132px
  # for the strip+dropdown, 108px still free for windows below). Must
  # match sim_main.c/app_main.c's own
  # kernel_spawn_app(MY_APP_NAME, 0, 0, 320, TOTAL_H, 0) call -- desktop's
  # registered window has to be exactly this tall for the router's normal
  # (non-strip) hit-testing to ever find desktop down here at all. Synced
  # by comment on both sides, same as SCREEN_W above.
  MAX_LAUNCHER_ITEMS = 6
  ITEM_H = 18
  DROPDOWN_H = ITEM_H * MAX_LAUNCHER_ITEMS
  TOTAL_H = STRIP_H + DROPDOWN_H

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
    # no-op almost all of the time. The dropdown never goes stale on its
    # own (it's a fixed table, not live window state), so it never needs
    # this at all.
    return if @mode == :launcher
    redraw_if_changed
  end

  # Full redraw of the strip; the dropdown (if open) is drawn separately by
  # open_menu, since it needs to happen only once, right when opening, not
  # on every idle tick.
  def redraw
    draw_strip
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
    draw_strip(windows)
    draw_menu_button
  end

  def on_touch(x, y, pressed)
    # The router sends a TOUCH event on every ~16ms tick for as long as the
    # mouse stays held, not just once on the initial press (demo_touch
    # relies on exactly that, to draw a continuous trail while dragging).
    # Without this guard, holding down on the Menu slot re-toggled the
    # mode on every single one of those ticks -- Menu/Back/Menu/Back --
    # which looked like intense flicker (reported live); holding on a
    # dropdown row would have been worse, rapid-firing acid_launcher_spawn
    # for as long as it was held. A plain click should act once per press.
    unless pressed
      @touch_held = false
      return
    end
    return if @touch_held
    @touch_held = true

    if y < STRIP_H
      # The strip row: Menu/Back, or a normal taskbar entry. Never both --
      # in_menu_slot? is checked first, so it always wins ties.
      if in_menu_slot?(x)
        ( @mode == :launcher ) ? close_menu : open_menu
        return
      end
      return unless @mode == :windows
      slot = x / BUTTON_W
      return if slot >= MAX_TASKBAR_SLOTS
      entry = active_windows[slot]
      return unless entry
      acid_activate_window(entry[0])
      redraw_if_changed
      return
    end

    # Below the strip: only meaningful while the dropdown is actually open
    # (desktop only ever wins this region's hit-test then -- see TOTAL_H's
    # comment). If the dropdown is closed, desktop still technically owns
    # this rect (it's part of its registered window, for when the dropdown
    # IS open) -- but the router's own generic body-touch handling (this
    # event only reaches Ruby at all because desktop won a hit-test) has
    # ALREADY called kernel_router_activate_window on desktop before this
    # method even runs, unconditionally, for any click landing on any
    # window's body. Landing here specifically means the click was on
    # empty desktop background -- no real window happened to cover that
    # point -- and that automatic raise would otherwise leave desktop
    # incorrectly stuck on top of this region (winning future hit-tests
    # meant for whatever's normally there) until something else happens
    # to get activated. Give it back immediately; nothing actually needs
    # to change on screen since desktop draws nothing here anyway.
    unless @mode == :launcher
      acid_send_self_to_back
      return
    end
    row = ( y - STRIP_H ) / ITEM_H
    if row >= 0 && row < MAX_LAUNCHER_ITEMS && row < acid_launcher_count
      acid_launcher_spawn(row)
    end
    close_menu
  end

  private

  def open_menu
    @mode = :launcher
    idx = my_window_index
    # Raise desktop above every other window so its now-larger registered
    # bounds (TOTAL_H tall, not just STRIP_H) actually win hit-tests below
    # the strip, and so the dropdown visibly draws on top of whatever's
    # there. acid_activate_window is a no-op repaint-wise here (harmless)
    # since desktop draws the dropdown itself, right below.
    acid_activate_window(idx) if idx
    draw_strip
    draw_dropdown
    draw_menu_button
  end

  def close_menu
    @mode = :windows
    # Give back both things open_menu claimed: z-order (so desktop stops
    # winning hit-tests for whatever window actually lives below the
    # strip) and the screen region itself (so that window's real content
    # is visibly there again, not desktop's now-stale dropdown pixels).
    # Order matters -- send_self_to_back must happen BEFORE the repaint,
    # so the repaint's own z-order walk correctly treats desktop as the
    # bottom-most window for this region instead of the topmost.
    acid_send_self_to_back
    acid_repaint_region(0, STRIP_H, SCREEN_W, DROPDOWN_H)
    redraw
  end

  # Desktop's own kernel index -- active_windows deliberately excludes it
  # (see its own comment), so this does the same unfiltered scan to find
  # it specifically. Only needed for the one acid_activate_window(idx)
  # call in open_menu.
  def my_window_index
    i = 0
    max = acid_window_max
    while i < max
      info = acid_window_info(i)
      return i if info && info[0] == MY_APP_NAME
      i += 1
    end
    nil
  end

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

  def draw_strip(windows = active_windows)
    acid_fill_rect(0, 0, SCREEN_W, STRIP_H, BG_COLOR)
    windows.first(MAX_TASKBAR_SLOTS).each_with_index do |entry, slot|
      draw_button(slot, entry[1])
    end
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

  # A proper vertical list, one row per launchable app, filling the whole
  # width directly under the strip -- the actual floating-menu look, as
  # opposed to squeezing entries into the same horizontal button slots the
  # taskbar uses.
  def draw_dropdown
    acid_fill_rect(0, STRIP_H, SCREEN_W, DROPDOWN_H, BG_COLOR)
    count = acid_launcher_count
    i = 0
    while i < count && i < MAX_LAUNCHER_ITEMS
      y = STRIP_H + i * ITEM_H
      acid_draw_text(short_name(acid_launcher_path(i)), 6, y + 4, TEXT_COLOR, BG_COLOR)
      i += 1
    end
  end

  # Always the same reserved rightmost slot (MENU_SLOT_X) -- highlighted
  # like a focused window button while the dropdown is open, as a "you are
  # here, tap to close" cue; plain otherwise.
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
