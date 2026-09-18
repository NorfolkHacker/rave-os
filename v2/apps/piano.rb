class Piano < AcidApp
  WINDOW_W = 200
  WINDOW_H = 100
  TITLE_BAR_H = 16

  KEY_COUNT = 8
  KEY_W = WINDOW_W / KEY_COUNT
  KEY_Y = TITLE_BAR_H
  KEY_H = WINDOW_H - TITLE_BAR_H

  BG_COLOR = 0x050607      # THEME_BG
  KEY_COLOR = 0xD4E6DB     # THEME_TEXT
  KEY_BORDER = 0x9DAAA3    # THEME_MUTED
  PRESSED_COLOR = 0x00FF66 # THEME_HARD

  VOICE = 5
  FILTER_MODE_LP = 1
  # One octave of a major scale (semitone offsets from the root) rather
  # than 8 flat chromatic steps -- distinctly "play a tune" musical,
  # instead of a slide whistle.
  SCALE_OFFSETS = [ 0, 2, 4, 5, 7, 9, 11, 12 ]
  ROOT_ONA = 40

  def on_create
    # Same filter this codebase's other synth users (acid_blaster,
    # breakout) already configure -- a mild low-pass warms the default
    # raw pulse wave instead of leaving it harsh. Unlike those, this
    # voice gets a real sustain and a natural release instead of a short
    # percussive envelope: a held piano key should keep sounding while
    # held, then fade out on release, not blip and cut off.
    acid_configure_filter(180, 3, FILTER_MODE_LP)
    acid_configure_voice(VOICE, 1, 5, 80, 60, 120)
    @pressed_key = nil
  end

  def key_at(x)
    key = x / KEY_W
    key = 0 if key < 0
    key = KEY_COUNT - 1 if key >= KEY_COUNT
    key
  end

  def on_touch(x, y, pressed)
    unless pressed
      return unless @pressed_key
      acid_stop_note(VOICE)
      old = @pressed_key
      @pressed_key = nil
      draw_key(old)
      return
    end

    key = key_at(x)
    return if key == @pressed_key
    old = @pressed_key
    @pressed_key = key
    ona = ROOT_ONA + SCALE_OFFSETS[key]
    acid_play_note(VOICE, ona, 45)
    draw_key(old) if old
    draw_key(key)
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    i = 0
    while i < KEY_COUNT
      draw_key(i)
      i += 1
    end
    acid_draw_window_border
  end

  def draw_key(index)
    x = index * KEY_W
    bg = ( index == @pressed_key ) ? PRESSED_COLOR : BG_COLOR
    fg = ( index == @pressed_key ) ? BG_COLOR : KEY_COLOR
    acid_fill_rect(x, KEY_Y, KEY_W, KEY_H, bg)
    acid_draw_text("#{index + 1}", x + KEY_W / 2 - 3, KEY_Y + KEY_H / 2 - 4, fg, bg)
    acid_fill_rect(x, KEY_Y, 1, KEY_H, KEY_BORDER)
  end
end

Piano.new.start
