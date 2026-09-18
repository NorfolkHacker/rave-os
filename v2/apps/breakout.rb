class Breakout < AcidGame
  WINDOW_W = 200
  WINDOW_H = 160
  TITLE_BAR_H = 16

  BG_COLOR = 0x050607      # THEME_BG
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT

  BRICK_COLS = 8
  BRICK_ROWS = 4
  BRICK_GAP = 1
  BRICK_W = ( WINDOW_W - ( BRICK_COLS + 1 ) * BRICK_GAP ) / BRICK_COLS
  BRICK_H = 8
  BRICK_TOP = TITLE_BAR_H + 4
  # Cycles through the same theme accents demo_swatch.rb already uses --
  # no new colors invented for this app.
  BRICK_COLORS = [ 0x00FF66, 0xD4E6DB, 0x9DAAA3, 0x00FF66 ]

  PADDLE_W = 32
  PADDLE_H = 4
  PADDLE_Y = WINDOW_H - 10

  BALL_R = 2
  BALL_SPEED = 2

  # Distinct voices from acid_blaster's (0/1) so the two never fight over
  # the same voice if somehow both were open at once.
  BRICK_VOICE = 2
  PADDLE_VOICE = 3
  OVER_VOICE = 4
  FILTER_MODE_LP = 1

  BRICK_NOTES = [ 60, 57, 1, 1 ]
  PADDLE_NOTES = [ 40, 44, 1, 1 ]
  OVER_NOTES = [ 30, 27, 23, 18 ]

  def on_create
    acid_configure_filter(180, 3, FILTER_MODE_LP)
    acid_configure_voice(BRICK_VOICE, 1, 2, 30, 50, 50)
    acid_configure_voice(PADDLE_VOICE, 1, 2, 30, 50, 50)
    acid_configure_voice(OVER_VOICE, 1, 8, 100, 35, 100)
    reset_game
  end

  def reset_game
    @score = 0
    @game_over = false
    @win = false
    @paddle_x = ( WINDOW_W - PADDLE_W ) / 2
    @ball = { x: WINDOW_W / 2, y: PADDLE_Y - BALL_R - 1, dx: BALL_SPEED, dy: -BALL_SPEED }
    @bricks = []
    row = 0
    while row < BRICK_ROWS
      col = 0
      while col < BRICK_COLS
        @bricks << {
          x: BRICK_GAP + col * ( BRICK_W + BRICK_GAP ),
          y: BRICK_TOP + row * ( BRICK_H + BRICK_GAP ),
          color: BRICK_COLORS[ row % BRICK_COLORS.length ],
          alive: true
        }
        col += 1
      end
      row += 1
    end
    @sfx = []
    @just_destroyed = []
    @drawn_ball = nil
    @drawn_paddle_x = nil
    @drawn_game_over = false
    @needs_frame = true
    @was_focused = false
  end

  def trigger_sfx(voice, notes, count, rate_ms, volume, ticks)
    acid_play_note(voice, notes[0], volume)
    acid_trigger_arp(voice, notes[0], notes[1], notes[2], notes[3], count, rate_ms)
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

  def on_touch(x, y, pressed)
    return unless pressed
    if @game_over
      reset_game
      return
    end
    @paddle_x = x - PADDLE_W / 2
    @paddle_x = 0 if @paddle_x < 0
    @paddle_x = WINDOW_W - PADDLE_W if @paddle_x > WINDOW_W - PADDLE_W
  end

  def on_tick
    unless @game_over
      update_ball
    end
    tick_sfx
    is_focused = focused?
    @needs_frame = true if is_focused && !@was_focused
    @was_focused = is_focused
    draw if is_focused
  end

  def update_ball
    b = @ball
    b[:x] += b[:dx]
    b[:y] += b[:dy]

    if b[:x] - BALL_R < 0
      b[:x] = BALL_R
      b[:dx] = -b[:dx]
    elsif b[:x] + BALL_R > WINDOW_W - 1
      b[:x] = WINDOW_W - 1 - BALL_R
      b[:dx] = -b[:dx]
    end
    if b[:y] - BALL_R < TITLE_BAR_H
      b[:y] = TITLE_BAR_H + BALL_R
      b[:dy] = -b[:dy]
    end

    if b[:y] + BALL_R >= PADDLE_Y && b[:y] + BALL_R <= PADDLE_Y + PADDLE_H &&
       b[:dy] > 0 && b[:x] >= @paddle_x - BALL_R && b[:x] <= @paddle_x + PADDLE_W + BALL_R
      b[:dy] = -b[:dy]
      offset = b[:x] - ( @paddle_x + PADDLE_W / 2 )
      b[:dx] = BALL_SPEED + offset / 6
      b[:dx] = -3 if b[:dx] < -3
      b[:dx] = 3 if b[:dx] > 3
      b[:dx] = 1 if b[:dx] == 0
      trigger_sfx(PADDLE_VOICE, PADDLE_NOTES, 2, 20, 25, 2)
    end

    if b[:y] - BALL_R > WINDOW_H - 1
      @game_over = true
      trigger_sfx(OVER_VOICE, OVER_NOTES, 4, 45, 40, 4)
      return
    end

    hit = nil
    @bricks.each do |brick|
      next unless brick[:alive]
      next unless b[:x] + BALL_R >= brick[:x] && b[:x] - BALL_R <= brick[:x] + BRICK_W &&
                  b[:y] + BALL_R >= brick[:y] && b[:y] - BALL_R <= brick[:y] + BRICK_H
      hit = brick
      break
    end
    return unless hit
    hit[:alive] = false
    @just_destroyed << hit
    @score += 1
    b[:dy] = -b[:dy]
    trigger_sfx(BRICK_VOICE, BRICK_NOTES, 2, 15, 30, 2)

    return if @bricks.any? { |brick| brick[:alive] }
    @win = true
    @game_over = true
    trigger_sfx(OVER_VOICE, OVER_NOTES.reverse, 4, 45, 40, 4)
  end

  def draw
    if @needs_frame
      acid_clear_user_area
      acid_draw_window_frame(window_title)
      @bricks.each { |brick| draw_brick(brick) if brick[:alive] }
      @drawn_paddle_x = nil
      @drawn_ball = nil
      @drawn_game_over = false
      acid_draw_window_border
      @needs_frame = false
    end

    unless @just_destroyed.empty?
      @just_destroyed.each { |brick| erase_rect(brick[:x], brick[:y], BRICK_W, BRICK_H) }
      @just_destroyed = []
    end

    if @game_over
      unless @drawn_game_over
        erase_ball
        erase_paddle
        draw_game_over
        @drawn_game_over = true
      end
      return
    end

    erase_ball
    acid_fill_circle(@ball[:x], @ball[:y], BALL_R, TEXT_COLOR)
    @drawn_ball = { x: @ball[:x], y: @ball[:y] }

    return if @drawn_paddle_x == @paddle_x
    erase_paddle
    acid_fill_rect(@paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, TEXT_COLOR)
    @drawn_paddle_x = @paddle_x
  end

  def draw_brick(brick)
    acid_fill_rect(brick[:x], brick[:y], BRICK_W, BRICK_H, brick[:color])
  end

  def erase_rect(x, y, w, h)
    acid_fill_rect(x, y, w, h, BG_COLOR)
  end

  def erase_ball
    return unless @drawn_ball
    acid_fill_circle(@drawn_ball[:x], @drawn_ball[:y], BALL_R, BG_COLOR)
    @drawn_ball = nil
  end

  def erase_paddle
    return unless @drawn_paddle_x
    acid_fill_rect(@drawn_paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, BG_COLOR)
    @drawn_paddle_x = nil
  end

  def draw_game_over
    label = @win ? "YOU WIN" : "GAME OVER"
    acid_draw_text(label, ( WINDOW_W - label.length * 6 ) / 2, WINDOW_H / 2 - 10, TEXT_COLOR, BG_COLOR)
    score_label = "SCORE: #{@score}"
    acid_draw_text(score_label, ( WINDOW_W - score_label.length * 6 ) / 2, WINDOW_H / 2 + 4, TEXT_COLOR, BG_COLOR)
  end
end

Breakout.new.start
