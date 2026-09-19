# Config -- system-wide settings. Deliberately just one real knob for now
# (master output volume, via kernel_audio's own global gain stage): every
# other candidate "setting" in this codebase (per-app title colors, window
# layout) is either a compile-time constant no runtime code ever reads
# again, or has no shared state to adjust in the first place -- a toggle
# that changes nothing when tapped is worse than not having it. Add a
# section here only when there's a real acid_* binding backing it.
class ConfigApp < AcidApp
  WINDOW_W = 180
  WINDOW_H = 110
  TITLE_BAR_H = 16

  TEXT_COLOR = 0xD4E6DB   # THEME_TEXT
  MUTED_COLOR = 0x9DAAA3  # THEME_MUTED
  BG_COLOR = 0x050607     # THEME_BG
  PANEL_COLOR = 0x0B1712  # THEME_PANEL
  HARD_COLOR = 0x00FF66   # THEME_HARD

  BTN_SIZE = 20
  STEP = 10

  BAR_X = 4
  BAR_Y = 44
  BAR_W = 172
  BAR_H = 14

  def on_create
    # Reads the kernel's actual current gain rather than assuming 100 --
    # if Config is closed and reopened after another session already
    # changed it, this must show the real value, not silently reset it.
    @volume = acid_get_volume
  end

  def window_title
    "Config"
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)

    y = TITLE_BAR_H + 4
    acid_draw_text("VOLUME", 4, y, MUTED_COLOR, BG_COLOR)
    acid_draw_text("#{@volume}%", WINDOW_W - 4 - "#{@volume}%".length * 6, y, TEXT_COLOR, BG_COLOR)

    acid_fill_rect(BAR_X, BAR_Y, BAR_W, BAR_H, PANEL_COLOR)
    filled = BAR_W * @volume / 100
    acid_fill_rect(BAR_X, BAR_Y, filled, BAR_H, HARD_COLOR) if filled > 0

    btn_y = BAR_Y + BAR_H + 8
    draw_button(BAR_X, btn_y, "-")
    draw_button(WINDOW_W - BAR_X - BTN_SIZE, btn_y, "+")

    acid_draw_window_border
  end

  def draw_button(x, y, label)
    acid_fill_rect(x, y, BTN_SIZE, BTN_SIZE, PANEL_COLOR)
    acid_draw_text(label, x + BTN_SIZE / 2 - 3, y + BTN_SIZE / 2 - 4, TEXT_COLOR, PANEL_COLOR)
  end

  # The router resends a TOUCH event on every ~16ms tick for as long as
  # the mouse stays held (see desktop.rb's own on_touch comment on this
  # exact behavior) -- without this guard, one press-and-hold on a button
  # fired the +/- step a dozen-plus times instead of once, confirmed live
  # (a single click on "-" jumped straight from 100% to 0%).
  def on_touch(x, y, pressed)
    unless pressed
      @touch_held = false
      return
    end
    return if @touch_held
    @touch_held = true

    btn_y = BAR_Y + BAR_H + 8
    return unless y >= btn_y && y < btn_y + BTN_SIZE

    if x >= BAR_X && x < BAR_X + BTN_SIZE
      set_volume(@volume - STEP)
    elsif x >= WINDOW_W - BAR_X - BTN_SIZE && x < WINDOW_W - BAR_X
      set_volume(@volume + STEP)
    end
  end

  def set_volume(v)
    v = 0 if v < 0
    v = 100 if v > 100
    return if v == @volume
    @volume = v
    acid_set_volume(@volume)
    redraw
  end
end

ConfigApp.new.start
