# The terminal's three easter eggs (see apps/terminal.rb): `dave` flies a
# superman across the screen, `joe` flies a teapot, `maximbady` bounces a
# figure around and then shouts. All three draw on the kernel overlay
# (core/kernel/kernel_overlay.h) -- over the wallpaper, the taskbar and
# every open window.
#
# A module, not an app, and deliberately so. Spawning an app for this would
# have cost an 8KB FreeRTOS task stack, a whole mrb_open() and six Ruby
# files compiled from source (kernel_spawn.c:75, vm_host.c:260-276) every
# time someone types `dave`. Instead the terminal drives it from its own
# event loop: see AcidApp#poll_timeout_ms and TerminalApp#on_idle.
#
# Every entry point takes an explicit now_ms so the whole animation can be
# stepped deterministically with no clock (tools/test_acid_eggs.rb).
module AcidEggs
  SCREEN_W = 640   # KERNEL_SCREEN_W (core/kernel/kernel_layout.h)
  SCREEN_H = 360   # KERNEL_SCREEN_H
  TICK_MS = 33     # ~30fps
  SCALE = 3

  # Voices 0-4 belong to the games (acid_blaster.rb, breakout.rb) and 5 to
  # the piano (piano.rb) out of SYNTH_NUM_VOICES 8, so 6 is free.
  VOICE = 6
  NOTE_TICKS = 6

  FLY_SPEED = 7
  BOUNCE_SPEED = 5
  BOUNCE_LIMIT = 6
  WORD_FRAMES = 60

  # A bob table instead of Math.sin: no dependency on whether this mruby
  # build carries mruby-math, and an integer pixel offset is what actually
  # gets drawn anyway.
  BOB = [ 0, 1, 2, 3, 3, 2, 1, 0, -1, -2, -3, -3, -2, -1 ]

  PALETTE = {
    "K" => 0x101010,   # hair, outline
    "S" => 0xE8B48A,   # skin
    "B" => 0x2050E0,   # superman blue / maximbady trousers
    "R" => 0xE01020,   # cape / maximbady top
    "Y" => 0xFFD400,   # chest emblem
    "W" => 0xE8E8F0,   # teapot body
    "G" => 0x9AA4B0,   # teapot shadow
    "N" => 0x18B830    # maximbady green
  }

  # Flying right: cape trails to the LEFT, arms reach right. AcidSprite's
  # flip handles the other direction.
  DAVE = [ "......KKKK....",
           ".....KSSSSK...",
           "RR...KSSSSK...",
           "RRR..KSSSSK...",
           "RRRRBBBBBBSSS.",
           "RRRRBBYBBBSSSS",
           "RRRRBBBBBBSSS.",
           "RRR..BBBBB....",
           "RR...BB..BB...",
           ".....RR..RR..." ]

  JOE = [ "....WWWWW.....",
          "...WWWWWWW....",
          "..WWWWWWWWW.WW",
          "WWWWWWWWWWWWWW",
          "WWWWWWWWWWWW.W",
          "WWWWWWWWWWW...",
          ".GWWWWWWWWG...",
          "..GGGGGGGG...." ]

  # Blue trousers, red top, green head and arms, exactly as asked for.
  MAXIMBADY = [ "..NNN..",
                "..NNN..",
                "N.RRR.N",
                "NRRRRRN",
                "NRRRRRN",
                "..RRR..",
                "..BBB..",
                "..B.B..",
                "..B.B.." ]

  # The two glyphs "SOOOOOOOOO" needs, at 5x7. The built-in 6px font would
  # render the whole word 60px wide -- far too small for a screen-centre
  # gag, which is why the word is drawn as sprites like everything else.
  GLYPH_SCALE = 6
  GLYPH_GAP = 6
  FONT = {
    "S" => [ ".YYYY",
             "Y....",
             "Y....",
             ".YYY.",
             "....Y",
             "....Y",
             "YYYY." ],
    "O" => [ ".YYY.",
             "Y...Y",
             "Y...Y",
             "Y...Y",
             "Y...Y",
             "Y...Y",
             ".YYY." ]
  }
  WORD = "SOOOOOOOOO"

  def self.names
    [ "dave", "joe", "maximbady" ]
  end

  def self.active?
    !@egg.nil?
  end

  def self.start(name, now_ms = nil)
    return false if active?
    return false unless names.include?(name)
    return false unless acid_overlay_open

    @egg = name
    @now = now_ms.nil? ? now_millis : now_ms
    # Deliberately NOT backdated to @now - TICK_MS. start() draws nothing
    # itself, so the first visible frame always comes from the first step()
    # call, which the terminal's event loop reaches within one poll interval
    # regardless -- backdating would only have bought back that one frame
    # (33ms) of latency, which is imperceptible. What backdating actually
    # costs is real: it makes step()'s own elapsed-time guard vacuous, since
    # any step() called after a backdated start already shows a full
    # TICK_MS elapsed and fires immediately no matter how soon it is called.
    # That guard exists to stop the animation free-running off however
    # often the caller happens to poll, and a guard that can never withhold
    # a frame is not a guard.
    @last_ms = @now
    @frame = 0
    @note_ticks = 0
    @phase = :fly

    acid_configure_voice(VOICE, 1, 4, 60, 40, 90)

    if name == "maximbady"
      @sprite = MAXIMBADY
      @bounces = 0
      @x = 40
      @y = 40
      @vx = BOUNCE_SPEED
      @vy = BOUNCE_SPEED
      @word_frames = 0
    else
      @sprite = (name == "dave") ? DAVE : JOE
      w = AcidSprite.width(@sprite) * SCALE
      h = AcidSprite.height(@sprite) * SCALE
      # Random height that keeps the whole sprite on screen even at the
      # extremes of the bob, and a random direction each time.
      bob = 3 * SCALE
      @y = bob + rand(SCREEN_H - h - 2 * bob)
      @flip = rand(2) == 0
      if @flip
        @x = SCREEN_W
        @vx = -FLY_SPEED
      else
        @x = -w
        @vx = FLY_SPEED
      end
      whoosh
    end

    true
  end

  def self.step(now_ms = nil)
    return unless active?
    now = now_ms.nil? ? now_millis : now_ms
    return if now - @last_ms < TICK_MS
    @last_ms = now
    @frame += 1

    tick_note

    if @phase == :word
      step_word
    elsif @egg == "maximbady"
      step_bounce
    else
      step_fly
    end
  end

  def self.abort
    return unless active?
    acid_stop_note(VOICE)
    acid_overlay_close
    @egg = nil
    @phase = nil
  end

  # ------------------------------------------------------------- phases

  def self.step_fly
    @x += @vx
    w = AcidSprite.width(@sprite) * SCALE
    if @x > SCREEN_W || @x + w < 0
      abort
      return
    end

    bob = BOB[@frame % BOB.length] * ((@egg == "dave") ? 1 : 0)
    wobble = (@egg == "joe") ? BOB[(@frame / 2) % BOB.length] / 2 : 0
    acid_overlay_clear
    AcidSprite.draw(@sprite, @x, @y + bob + wobble, SCALE, PALETTE, @flip)
  end

  def self.step_bounce
    w = AcidSprite.width(@sprite) * SCALE
    h = AcidSprite.height(@sprite) * SCALE

    @x += @vx
    @y += @vy

    bounced = false
    if @x <= 0
      @x = 0
      @vx = -@vx
      bounced = true
    elsif @x + w >= SCREEN_W
      @x = SCREEN_W - w
      @vx = -@vx
      bounced = true
    end
    if @y <= 0
      @y = 0
      @vy = -@vy
      bounced = true
    elsif @y + h >= SCREEN_H
      @y = SCREEN_H - h
      @vy = -@vy
      bounced = true
    end

    if bounced
      @bounces += 1
      boing
    end

    if @bounces >= BOUNCE_LIMIT
      @phase = :word
      @word_frames = 0
      slide
      acid_overlay_clear
      return
    end

    acid_overlay_clear
    AcidSprite.draw(@sprite, @x, @y, SCALE, PALETTE)
  end

  def self.step_word
    @word_frames += 1
    if @word_frames > WORD_FRAMES
      abort
      return
    end

    # Redrawn every frame rather than drawn once: the overlay canvas is not
    # persistent across an app's other drawing, and a single clear+draw per
    # frame is the same path every other phase uses.
    acid_overlay_clear
    draw_word
  end

  # [x, y, w, h] of the space the whole word occupies, centred on screen.
  # Separate from draw_word because the glyphs have blank columns in some of
  # their rows, so the drawn pixels' own bounding box is narrower than this
  # and is the wrong thing for anything (a test, a future backdrop) to
  # measure centring against.
  def self.word_box
    glyph_w = 5 * GLYPH_SCALE
    glyph_h = 7 * GLYPH_SCALE
    total = WORD.length * glyph_w + (WORD.length - 1) * GLYPH_GAP
    [ (SCREEN_W - total) / 2, (SCREEN_H - glyph_h) / 2, total, glyph_h ]
  end

  def self.draw_word
    box = word_box
    glyph_w = 5 * GLYPH_SCALE
    i = 0
    while i < WORD.length
      AcidSprite.draw(FONT[WORD[i, 1]], box[0] + i * (glyph_w + GLYPH_GAP), box[1],
                      GLYPH_SCALE, PALETTE)
      i += 1
    end
  end

  # -------------------------------------------------------------- sound

  def self.whoosh
    if @egg == "dave"
      acid_play_note(VOICE, 28, 40)
      acid_trigger_arp(VOICE, 28, 35, 40, 47, 4, 60)
    else
      acid_play_note(VOICE, 52, 35)
      acid_trigger_arp(VOICE, 52, 56, 52, 56, 2, 90)
    end
    @note_ticks = NOTE_TICKS * 2
  end

  def self.boing
    acid_play_note(VOICE, 40 + rand(8), 45)
    @note_ticks = NOTE_TICKS
  end

  def self.slide
    acid_play_note(VOICE, 50, 55)
    acid_trigger_arp(VOICE, 50, 45, 38, 31, 4, 110)
    @note_ticks = NOTE_TICKS * 4
  end

  def self.tick_note
    return if @note_ticks <= 0
    @note_ticks -= 1
    acid_stop_note(VOICE) if @note_ticks == 0
  end

  def self.now_millis
    (Time.now.to_f * 1000).to_i
  end
end
