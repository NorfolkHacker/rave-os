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

  def on_create
    reset_game
  end

  def reset_game
    @score = 0
    @enemies = []
    @spawn_timer = 0
    @game_over = false
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
      x = rand(WINDOW_W)
      y = TITLE_BAR_H
    elsif edge == 1
      x = WINDOW_W
      y = TITLE_BAR_H + rand(PLAY_H)
    elsif edge == 2
      x = rand(WINDOW_W)
      y = TITLE_BAR_H + PLAY_H
    else
      x = 0
      y = TITLE_BAR_H + rand(PLAY_H)
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

  # Returns true if any enemy reached the center this tick.
  def update_enemies
    hit_center = false
    @enemies.each do |e|
      e[:x] += e[:dx]
      e[:y] += e[:dy]
      ddx = e[:x] - CENTER_X
      ddy = e[:y] - CENTER_Y
      hit_center = true if (ddx * ddx + ddy * ddy) <= (ENEMY_R * ENEMY_R)
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
      @game_over = true if update_enemies
    end
    draw
  end

  def draw
    acid_clear_user_area
    acid_draw_window_frame
    if @game_over
      draw_game_over
    else
      @enemies.each { |e| acid_fill_circle(e[:x], e[:y], ENEMY_R, ENEMY_COLOR) }
      acid_draw_text("SCORE: #{@score}", 4, TITLE_BAR_H + 2, TEXT_COLOR, BG_COLOR)
    end
  end

  def draw_game_over
    acid_draw_text("GAME OVER", CENTER_X - 36, CENTER_Y - 10, TEXT_COLOR, BG_COLOR)
    acid_draw_text("SCORE: #{@score}", CENTER_X - 30, CENTER_Y + 6, TEXT_COLOR, BG_COLOR)
  end
end

AcidBlaster.new.start
