class AcidBlaster < AcidGame
  # Must match the kernel_spawn_app(...) call that spawns this app (Task
  # 5) and kernel_layout.h's KERNEL_TITLE_BAR_H -- no generic "ask my own
  # window size" binding exists (see this plan's Global Constraints).
  WINDOW_W = 250
  WINDOW_H = 180
  TITLE_BAR_H = 16
  PLAY_H = WINDOW_H - TITLE_BAR_H
  CENTER_X = WINDOW_W / 2
  CENTER_Y = TITLE_BAR_H + PLAY_H / 2

  ENEMY_R = 8
  TAP_TOLERANCE = 6

  BG_COLOR = 0x050607     # THEME_BG
  ENEMY_COLOR = 0x00FF66  # THEME_HARD
  TEXT_COLOR = 0xD4E6DB   # THEME_TEXT

  HIT_VOICE = 0
  HIT_ONA = 55
  HIT_TICKS = 3
  OVER_VOICE = 1
  OVER_ONA = 25
  OVER_TICKS = 8

  # By default every synth voice is an unfiltered pulse wave with an
  # instant attack and instant release (see synth_init()'s own comment) --
  # a hard digital click on every hit, harsh even at a short duration.
  # Routing both SFX voices through the shared filter and giving them a
  # real (if brief) envelope turns that click into a short, filtered
  # "acid" blip instead. Configured once here, not per-hit in
  # trigger_sfx -- these are voice-wide settings, not per-note.
  FILTER_MODE_LP = 1
  def on_create
    acid_configure_filter(180, 3, FILTER_MODE_LP)
    acid_configure_voice(HIT_VOICE, 1, 3, 40, 55, 70)
    acid_configure_voice(OVER_VOICE, 1, 8, 150, 35, 250)
    reset_game
  end

  def reset_game
    @score = 0
    @enemies = []
    @spawn_timer = 0
    @game_over = false
    @sfx = []
    # Dirty-tracking state for draw() -- see its own comment for why this
    # exists. needs_frame starts true so the very first draw does a full
    # clear+chrome paint.
    @drawn_enemies = []
    @drawn_score = nil
    @drawn_game_over = false
    @needs_frame = true
    @was_focused = false
  end

  def trigger_sfx(voice, ona, volume, ticks)
    acid_play_note(voice, ona, volume)
    @sfx << { voice: voice, ticks: ticks }
  end

  def tick_sfx
    i = @sfx.length - 1
    while i >= 0
      s = @sfx[i]
      s[:ticks] -= 1
      if s[:ticks] <= 0
        acid_stop_note(s[:voice])
        @sfx.delete_at(i)
      end
      i -= 1
    end
  end

  def spawn_interval
    v = 24 - @score / 2
    v < 6 ? 6 : v
  end

  def enemy_speed
    v = 2 + @score / 5
    v > 8 ? 8 : v
  end

  def spawn_enemy
    edge = rand(4)
    if edge == 0
      x = ENEMY_R + rand(WINDOW_W - 2 * ENEMY_R)
      y = TITLE_BAR_H + ENEMY_R
    elsif edge == 1
      x = WINDOW_W - 1 - ENEMY_R
      y = TITLE_BAR_H + ENEMY_R + rand(PLAY_H - 2 * ENEMY_R)
    elsif edge == 2
      x = ENEMY_R + rand(WINDOW_W - 2 * ENEMY_R)
      y = TITLE_BAR_H + PLAY_H - 1 - ENEMY_R
    else
      x = ENEMY_R
      y = TITLE_BAR_H + ENEMY_R + rand(PLAY_H - 2 * ENEMY_R)
    end

    dx = CENTER_X - x
    dy = CENTER_Y - y
    dist = Math.sqrt((dx * dx + dy * dy).to_f)
    dist = 1.0 if dist < 1.0
    speed = enemy_speed
    vx = (dx * speed / dist).round
    vy = (dy * speed / dist).round
    # Belt-and-braces: with speed >= 2 this cannot actually happen (see
    # the design spec's note), but a stuck enemy would be a silent, very
    # confusing bug if it ever did, so guard it anyway.
    if vx == 0 && vy == 0
      vx = dx <=> 0
      vy = dy <=> 0
    end

    @enemies << { x: x, y: y, dx: vx, dy: vy }
  end

  # Returns true if any enemy reached the center this tick. Also removes
  # any enemy that has drifted off the play field without reaching the
  # center (an integer-rounded spawn direction can miss the center by
  # more than ENEMY_R -- see the final review's finding -- so nothing
  # would otherwise ever remove it, and it would fly off forever).
  def update_enemies
    hit_center = false
    i = @enemies.length - 1
    while i >= 0
      e = @enemies[i]
      e[:x] += e[:dx]
      e[:y] += e[:dy]
      ddx = e[:x] - CENTER_X
      ddy = e[:y] - CENTER_Y
      if (ddx * ddx + ddy * ddy) <= (ENEMY_R * ENEMY_R)
        hit_center = true
      elsif e[:x] - ENEMY_R < 0 || e[:x] + ENEMY_R > WINDOW_W - 1 ||
            e[:y] - ENEMY_R < TITLE_BAR_H || e[:y] + ENEMY_R > TITLE_BAR_H + PLAY_H - 1
        @enemies.delete_at(i)
      end
      i -= 1
    end
    hit_center
  end

  # Returns true if a tap at (x, y) destroyed an enemy.
  def check_tap(x, y)
    hit_index = nil
    i = 0
    while i < @enemies.length
      e = @enemies[i]
      ddx = x - e[:x]
      ddy = y - e[:y]
      limit = ENEMY_R + TAP_TOLERANCE
      if (ddx * ddx + ddy * ddy) <= (limit * limit)
        hit_index = i
        break
      end
      i += 1
    end
    return false unless hit_index
    @enemies.delete_at(hit_index)
    @score += 1
    trigger_sfx(HIT_VOICE, HIT_ONA, 90, HIT_TICKS)
    true
  end

  def on_touch(x, y, pressed)
    return unless pressed
    if @game_over
      reset_game
      return
    end
    check_tap(x, y)
  end

  def on_tick
    unless @game_over
      @spawn_timer -= 1
      if @spawn_timer <= 0
        spawn_enemy
        @spawn_timer = spawn_interval
      end
      if update_enemies
        @game_over = true
        trigger_sfx(OVER_VOICE, OVER_ONA, 90, OVER_TICKS)
      end
    end
    tick_sfx
    # Game state (enemy positions, spawns, game-over) keeps advancing
    # every tick regardless -- only the DRAWING is skipped while covered.
    # draw() has no z-order awareness at all (it paints straight onto the
    # shared framebuffer every tick, unlike a static window's redraw,
    # which only ever runs inside the compositor's own z-order-aware
    # repaint) -- if it kept drawing while genuinely behind another
    # window, it would paint over that window's visible content on every
    # single tick. Skipping it here means the window some other app has
    # on top stays correctly on top.
    is_focused = focused?
    # Something else painted over our window's pixels for however long we
    # were unfocused, so the incremental erase-old/draw-new below (which
    # assumes the framebuffer still shows what we last drew) is no longer
    # valid -- force one full clear+chrome repaint on the tick we regain
    # focus, same as the very first draw.
    @needs_frame = true if is_focused && !@was_focused
    @was_focused = is_focused
    draw if is_focused
  end

  # Redraws only what actually changed since the last draw call, instead
  # of a full acid_clear_user_area + window-frame + border every single
  # tick (20/sec while focused). That full-window clear was visible as a
  # flicker (a black flash the moment before enemies were redrawn, with
  # no double buffering to hide it -- see this codebase's documented "no
  # real compositor" gap) and was slow on the software renderer (a ~250x
  # 164px fill 20 times a second, for a scene that's mostly a handful of
  # small circles). Erasing just the previously-drawn enemy positions and
  # redrawing just the current ones touches a tiny fraction of that area.
  def draw
    if @needs_frame
      acid_clear_user_area
      acid_draw_window_frame(window_title)
      acid_draw_window_border
      @drawn_enemies = []
      @drawn_score = nil
      @drawn_game_over = false
      @needs_frame = false
    end

    if @game_over
      unless @drawn_game_over
        erase_drawn_enemies
        draw_game_over
        @drawn_game_over = true
      end
      return
    end

    erase_drawn_enemies
    @enemies.each { |e| acid_fill_circle(e[:x], e[:y], ENEMY_R, ENEMY_COLOR) }
    @drawn_enemies = @enemies.map { |e| { x: e[:x], y: e[:y] } }

    return if @drawn_score == @score
    acid_draw_text("SCORE: #{@score}", 4, TITLE_BAR_H + 2, TEXT_COLOR, BG_COLOR)
    @drawn_score = @score
  end

  def erase_drawn_enemies
    @drawn_enemies.each { |d| acid_fill_circle(d[:x], d[:y], ENEMY_R, BG_COLOR) }
    @drawn_enemies = []
  end

  def draw_game_over
    acid_draw_text("GAME OVER", CENTER_X - 36, CENTER_Y - 10, TEXT_COLOR, BG_COLOR)
    acid_draw_text("SCORE: #{@score}", CENTER_X - 30, CENTER_Y + 6, TEXT_COLOR, BG_COLOR)
  end
end

AcidBlaster.new.start
