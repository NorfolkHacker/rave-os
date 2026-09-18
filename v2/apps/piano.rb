class Piano < AcidApp
  WINDOW_W = 200
  WINDOW_H = 100
  TITLE_BAR_H = 16
  KEY_Y = TITLE_BAR_H
  KEY_H = WINDOW_H - TITLE_BAR_H

  BG_COLOR = 0x050607      # THEME_BG
  WHITE_COLOR = 0xD4E6DB   # THEME_TEXT
  BLACK_COLOR = 0x0B1712   # THEME_PANEL
  PRESSED_COLOR = 0x00FF66 # THEME_HARD

  VOICE = 5
  FILTER_MODE_LP = 1
  ROOT_ONA = 40

  # Standard one-octave piano layout, same shape a real keyboard has --
  # matches family-mruby's own piano.app.rb (flash/app/demo/piano.app.rb):
  # 7 white keys (C..B), 5 black keys sitting in the gaps between them
  # (none between E/F or B/C, same as a real keyboard). Values are
  # semitone offsets from ROOT_ONA, not raw ona -- WHITE_OFFSETS[0] is C.
  WHITE_OFFSETS = [ 0, 2, 4, 5, 7, 9, 11 ]
  BLACK_OFFSETS = [ 1, 3, 6, 8, 10 ]
  # Which white key each black key sits just after (0-indexed) -- e.g.
  # BLACK_OFFSETS[0] (C#) sits after WHITE_OFFSETS[0] (C).
  BLACK_AFTER_WHITE = [ 0, 1, 3, 4, 5 ]

  WHITE_COUNT = WHITE_OFFSETS.length
  KEY_W = WINDOW_W / WHITE_COUNT
  BLACK_W = KEY_W * 2 / 3
  BLACK_H = KEY_H * 3 / 5

  def on_create
    # Same filter this codebase's other synth users (acid_blaster,
    # breakout) already configure -- a mild low-pass warms the default
    # raw pulse wave instead of leaving it harsh. Unlike those, this
    # voice gets a real sustain and a natural release instead of a short
    # percussive envelope: a held piano key should keep sounding while
    # held, then fade out on release, not blip and cut off.
    acid_configure_filter(180, 3, FILTER_MODE_LP)
    acid_configure_voice(VOICE, 1, 5, 80, 60, 120)
    @active_offset = nil
  end

  # White key i spans [x, x+w) -- w is KEY_W for every key except the
  # last, which takes whatever's left over so the keys always cover the
  # full window width exactly, regardless of whether WINDOW_W happens to
  # divide evenly by 7 (it doesn't: 200 / 7 leaves a 4px remainder).
  def white_key_x(index)
    index * KEY_W
  end

  def white_key_w(index)
    return WINDOW_W - white_key_x(index) if index == WHITE_COUNT - 1
    KEY_W
  end

  def black_key_x(slot)
    white_index = BLACK_AFTER_WHITE[slot]
    white_key_x(white_index) + KEY_W - BLACK_W / 2
  end

  # Black keys are drawn on top of (and are narrower than) the white
  # keys, so a touch landing in a black key's rect must win the hit test
  # even though it's also geometrically inside a white key's rect.
  def hit_test(x)
    slot = 0
    while slot < BLACK_OFFSETS.length
      bx = black_key_x(slot)
      return BLACK_OFFSETS[slot] if x >= bx && x < bx + BLACK_W
      slot += 1
    end
    index = x / KEY_W
    index = WHITE_COUNT - 1 if index >= WHITE_COUNT
    WHITE_OFFSETS[index]
  end

  def on_touch(x, y, pressed)
    unless pressed
      return unless @active_offset
      acid_stop_note(VOICE)
      old = @active_offset
      @active_offset = nil
      redraw_offset(old)
      return
    end

    offset = hit_test(x)
    return if offset == @active_offset
    old = @active_offset
    @active_offset = offset
    acid_play_note(VOICE, ROOT_ONA + offset, 45)
    redraw_offset(old) if old
    redraw_offset(offset)
  end

  # Full clear + redraw every key -- correct, but only ever needed for a
  # genuine full repaint (the initial paint, or AcidApp#start's :moved
  # handler after a drag). NOT what on_touch calls per keypress -- doing
  # a full acid_clear_user_area on every single press/release flashed the
  # whole keyboard to blank before redrawing it (reported live as "its
  # redraw is flickery"). See redraw_offset for the actual per-press path.
  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    draw_white_keys
    draw_black_keys
    acid_draw_window_border
  end

  # Repaints only the one key whose highlight state just changed --
  # a white key also repaints any black key(s) that visually overlap its
  # edges (a black key straddles the boundary between two white keys),
  # since a plain fill_rect over just the white key would otherwise erase
  # the sliver of black key sitting on top of it. A black key never needs
  # this the other way around -- it's drawn on top of, and entirely
  # contained within, the white keys under it.
  def redraw_offset(offset)
    white_index = WHITE_OFFSETS.index(offset)
    if white_index
      draw_white_key(white_index)
      slot = 0
      while slot < BLACK_AFTER_WHITE.length
        wi = BLACK_AFTER_WHITE[slot]
        draw_black_key(slot) if wi == white_index || wi == white_index - 1
        slot += 1
      end
      return
    end
    black_slot = BLACK_OFFSETS.index(offset)
    draw_black_key(black_slot) if black_slot
  end

  def draw_white_keys
    i = 0
    while i < WHITE_COUNT
      draw_white_key(i)
      i += 1
    end
  end

  def draw_white_key(index)
    pressed = WHITE_OFFSETS[index] == @active_offset
    acid_fill_rect(white_key_x(index), KEY_Y, white_key_w(index), KEY_H,
                    pressed ? PRESSED_COLOR : WHITE_COLOR)
    # Dividing line against the previous key -- acid_fill_rect's clip
    # against the window's own bounds (see gfx_binding.c) means index 0's
    # line at x=0 draws harmlessly over the border's own left edge.
    acid_fill_rect(white_key_x(index), KEY_Y, 1, KEY_H, BLACK_COLOR)
  end

  def draw_black_keys
    slot = 0
    while slot < BLACK_OFFSETS.length
      draw_black_key(slot)
      slot += 1
    end
  end

  def draw_black_key(slot)
    pressed = BLACK_OFFSETS[slot] == @active_offset
    acid_fill_rect(black_key_x(slot), KEY_Y, BLACK_W, BLACK_H,
                    pressed ? PRESSED_COLOR : BLACK_COLOR)
  end
end

Piano.new.start
