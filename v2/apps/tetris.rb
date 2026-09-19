class Tetris < AcidGame
  WINDOW_W = 160
  WINDOW_H = 160
  TITLE_BAR_H = 16

  BG_COLOR = 0x050607      # THEME_BG
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  GRID_LINE = 0x0B1712     # THEME_PANEL

  CELL = 8
  COLS = 8
  ROWS = 16
  GRID_X = 4
  GRID_Y = TITLE_BAR_H + 4
  GRID_W = COLS * CELL
  GRID_H = ROWS * CELL

  PANEL_X = GRID_X + GRID_W + 8

  # One fall step every FALL_TICKS on_tick calls -- AcidGame's own TICK_MS
  # (50ms) times this is the real fall interval (10 * 50ms = 500ms/row).
  FALL_TICKS = 10

  # Same small accent palette every other app in this codebase already
  # draws from -- no new colors invented for this one either. Cycles by
  # piece index, purely for telling pieces apart, not standard per-piece
  # Tetris colors (this theme doesn't have seven distinct hues to spare).
  PIECE_COLORS = [ 0x00FF66, 0xD4E6DB, 0x9DAAA3, 0x00FF66, 0xD4E6DB, 0x9DAAA3, 0x00FF66 ]

  # Each piece: one cell layout per rotation state, as [x,y] pairs inside
  # a 4x4 box (rotated around that box's own center, not the cell grid) --
  # the standard, simplest-to-hardcode representation for a first cut with
  # no wall-kick table. O has one physical rotation, listed 4 times so
  # PIECES[name][rotation] is always valid regardless of rotation index.
  PIECES = {
    "I" => [ [[0,1],[1,1],[2,1],[3,1]], [[2,0],[2,1],[2,2],[2,3]],
             [[0,2],[1,2],[2,2],[3,2]], [[1,0],[1,1],[1,2],[1,3]] ],
    "O" => [ [[1,0],[2,0],[1,1],[2,1]], [[1,0],[2,0],[1,1],[2,1]],
             [[1,0],[2,0],[1,1],[2,1]], [[1,0],[2,0],[1,1],[2,1]] ],
    "T" => [ [[1,0],[0,1],[1,1],[2,1]], [[1,0],[1,1],[2,1],[1,2]],
             [[0,1],[1,1],[2,1],[1,2]], [[1,0],[0,1],[1,1],[1,2]] ],
    "S" => [ [[1,0],[2,0],[0,1],[1,1]], [[1,0],[1,1],[2,1],[2,2]],
             [[1,0],[2,0],[0,1],[1,1]], [[1,0],[1,1],[2,1],[2,2]] ],
    "Z" => [ [[0,0],[1,0],[1,1],[2,1]], [[2,0],[1,1],[2,1],[1,2]],
             [[0,0],[1,0],[1,1],[2,1]], [[2,0],[1,1],[2,1],[1,2]] ],
    "J" => [ [[0,0],[0,1],[1,1],[2,1]], [[1,0],[2,0],[1,1],[1,2]],
             [[0,1],[1,1],[2,1],[2,2]], [[1,0],[1,1],[0,2],[1,2]] ],
    "L" => [ [[2,0],[0,1],[1,1],[2,1]], [[1,0],[1,1],[1,2],[2,2]],
             [[0,1],[1,1],[2,1],[0,2]], [[0,0],[1,0],[1,1],[1,2]] ],
  }
  PIECE_NAMES = PIECES.keys

  VOICE = 6
  FILTER_MODE_LP = 1
  LINE_NOTES = [ 60, 64, 67, 72 ]
  OVER_NOTES = [ 40, 36, 32, 28 ]

  def on_create
    acid_configure_filter(180, 3, FILTER_MODE_LP)
    acid_configure_voice(VOICE, 1, 3, 40, 40, 60)
    @grid = Array.new(ROWS) { Array.new(COLS, nil) }
    @score = 0
    @game_over = false
    @fall_counter = 0
    @next_name = random_piece_name
    spawn_piece
  end

  def random_piece_name
    PIECE_NAMES[( Time.now.to_f * 1000 ).to_i % PIECE_NAMES.length]
  end

  def spawn_piece
    @piece_name = @next_name
    @next_name = random_piece_name
    @rotation = 0
    @px = COLS / 2 - 2
    @py = 0
    @game_over = true unless piece_fits?(@px, @py, @rotation)
    trigger_sfx(OVER_NOTES, 3, 25, 30, 3) if @game_over
  end

  def cells_for(name, rotation)
    PIECES[name][rotation]
  end

  def piece_fits?(px, py, rotation)
    cells_for(@piece_name, rotation).each do |cell|
      x = px + cell[0]
      y = py + cell[1]
      return false if x < 0 || x >= COLS || y >= ROWS
      next if y < 0
      return false if @grid[y][x]
    end
    true
  end

  def lock_piece
    cells_for(@piece_name, @rotation).each do |cell|
      x = @px + cell[0]
      y = @py + cell[1]
      @grid[y][x] = PIECE_COLORS[PIECE_NAMES.index(@piece_name)] if y >= 0
    end
    clear_lines
    spawn_piece
  end

  def clear_lines
    cleared = 0
    y = ROWS - 1
    while y >= 0
      if @grid[y].all? { |c| c }
        @grid.delete_at(y)
        @grid.insert(0, Array.new(COLS, nil))
        cleared += 1
      else
        y -= 1
      end
    end
    return if cleared == 0
    @score += cleared * cleared * 100
    trigger_sfx(LINE_NOTES, cleared > 2 ? 4 : 2, 20, 35, 3)
  end

  def trigger_sfx(notes, count, rate_ms, volume, ticks)
    acid_play_note(VOICE, notes[0], volume)
    acid_trigger_arp(VOICE, notes[0], notes[1], notes[2], notes[3], count, rate_ms)
    @sfx_ticks = ticks
  end

  def tick_sfx
    return unless @sfx_ticks
    @sfx_ticks -= 1
    if @sfx_ticks <= 0
      acid_stop_note(VOICE)
      @sfx_ticks = nil
    end
  end

  def on_tick
    tick_sfx
    unless @game_over
      @fall_counter += 1
      if @fall_counter >= FALL_TICKS
        @fall_counter = 0
        fall
      end
    end
    draw
  end

  def fall
    if piece_fits?(@px, @py + 1, @rotation)
      @py += 1
    else
      lock_piece
    end
  end

  # The router sends a TOUCH event on every ~16ms tick for as long as the
  # mouse stays held, not just once on the initial press (see
  # file_manager.rb's own comment on this same guard) -- without it, a
  # single held tap would repeatedly move or spin the piece for as long
  # as the mouse stayed down instead of acting once per tap.
  def on_touch(x, y, pressed)
    unless pressed
      @touch_held = false
      return
    end
    return if @touch_held
    @touch_held = true
    if @game_over
      on_create
      draw
      return
    end
    if x < WINDOW_W / 3
      move(-1)
    elsif x > WINDOW_W * 2 / 3
      move(1)
    else
      rotate_piece
    end
    draw
  end

  def on_key(code, pressed)
    return unless pressed
    if @game_over
      on_create
      draw
      return
    end
    if code == AcidKeys::LEFT
      move(-1)
    elsif code == AcidKeys::RIGHT
      move(1)
    elsif code == AcidKeys::UP
      rotate_piece
    elsif code == AcidKeys::DOWN
      fall
    end
    draw
  end

  def move(dx)
    @px += dx if piece_fits?(@px + dx, @py, @rotation)
  end

  def rotate_piece
    next_rotation = ( @rotation + 1 ) % 4
    @rotation = next_rotation if piece_fits?(@px, @py, next_rotation)
  end

  def draw
    return unless focused?
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    draw_grid
    draw_panel
    if @game_over
      acid_draw_text("GAME OVER", GRID_X + 4, GRID_Y + GRID_H / 2 - 4, TEXT_COLOR, BG_COLOR)
    end
    acid_draw_window_border
  end

  def draw_grid
    acid_fill_rect(GRID_X, GRID_Y, GRID_W, GRID_H, GRID_LINE)
    y = 0
    while y < ROWS
      x = 0
      while x < COLS
        color = @grid[y][x]
        acid_fill_rect(GRID_X + x * CELL + 1, GRID_Y + y * CELL + 1, CELL - 1, CELL - 1, color) if color
        x += 1
      end
      y += 1
    end
    unless @game_over
      cells_for(@piece_name, @rotation).each do |cell|
        cx = @px + cell[0]
        cy = @py + cell[1]
        next if cy < 0
        color = PIECE_COLORS[PIECE_NAMES.index(@piece_name)]
        acid_fill_rect(GRID_X + cx * CELL + 1, GRID_Y + cy * CELL + 1, CELL - 1, CELL - 1, color)
      end
    end
  end

  def draw_panel
    acid_draw_text("SCORE", PANEL_X, GRID_Y, TEXT_COLOR, BG_COLOR)
    acid_draw_text(@score.to_s, PANEL_X, GRID_Y + 10, TEXT_COLOR, BG_COLOR)
    acid_draw_text("NEXT", PANEL_X, GRID_Y + 26, TEXT_COLOR, BG_COLOR)
    cells_for(@next_name, 0).each do |cell|
      color = PIECE_COLORS[PIECE_NAMES.index(@next_name)]
      acid_fill_rect(PANEL_X + cell[0] * 6, GRID_Y + 36 + cell[1] * 6, 5, 5, color)
    end
  end
end

Tetris.new.start
