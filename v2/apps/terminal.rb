class TerminalApp < AcidApp
  WINDOW_W = 260
  WINDOW_H = 160
  TITLE_BAR_H = 16
  LINE_H = 10
  ROOT_DIR = "v2/fsroot"
  ROOT_SEGMENTS = ROOT_DIR.split("/")
  PROMPT = "$ "

  BODY_BG = 0x050607       # THEME_BG
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  PROMPT_COLOR = 0x00FF66  # THEME_HARD
  CURSOR_COLOR = 0x00FF66  # THEME_HARD

  def on_create
    @cwd = ROOT_DIR
    @lines = [ "acid OS v2 terminal -- type help", "" ]
    @input = ""
    @history = []
    @history_pos = 0
  end

  def visible_lines
    ( WINDOW_H - TITLE_BAR_H ) / LINE_H - 1
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    draw_scrollback
    draw_input_line
    acid_draw_window_border
  end

  def draw_scrollback
    y = TITLE_BAR_H
    n = visible_lines
    start = @lines.length > n ? @lines.length - n : 0
    i = start
    while i < @lines.length
      acid_fill_rect(0, y, WINDOW_W, LINE_H, BODY_BG)
      acid_draw_text(@lines[i][0, 42], 2, y + 1, TEXT_COLOR, BODY_BG)
      y += LINE_H
      i += 1
    end
    # Pad any remaining rows (fewer lines than fit) so old content from a
    # taller previous frame can never show through underneath.
    while y < WINDOW_H - LINE_H
      acid_fill_rect(0, y, WINDOW_W, LINE_H, BODY_BG)
      y += LINE_H
    end
  end

  def draw_input_line
    y = WINDOW_H - LINE_H
    acid_fill_rect(0, y, WINDOW_W, LINE_H, BODY_BG)
    text = PROMPT + @input
    acid_draw_text(text[0, 42], 2, y + 1, PROMPT_COLOR, BODY_BG)
    cx = 2 + text.length * 6
    acid_fill_rect(cx, y + LINE_H - 2, 6, 2, CURSOR_COLOR)
  end

  def on_key(code, pressed)
    return unless pressed
    if code == AcidKeys::ENTER
      submit
    elsif code == AcidKeys::BACKSPACE
      @input = @input[0, @input.length - 1] if @input.length > 0
    elsif code == AcidKeys::UP
      history_prev
    elsif code == AcidKeys::DOWN
      history_next
    elsif code >= 32 && code <= 126
      @input += code.chr
    end
    redraw
    # on_idle only fires when acid_poll_event times out, so a burst of
    # keystrokes would otherwise stall the animation. AcidEggs.step is
    # guarded by its own TICK_MS check, which makes this call free whenever
    # it isn't time for a frame yet.
    AcidEggs.step
  end

  def submit
    line = @input
    @lines << (PROMPT + line)
    @history << line unless line.strip.empty?
    @history_pos = @history.length
    @input = ""
    run_command(line.strip) unless line.strip.empty?
  end

  def history_prev
    return if @history.empty? || @history_pos <= 0
    @history_pos -= 1
    @input = @history[@history_pos]
  end

  def history_next
    return if @history_pos >= @history.length
    @history_pos += 1
    @input = @history_pos < @history.length ? @history[@history_pos] : ""
  end

  def run_command(line)
    # The easter eggs (apps/lib/acid_eggs.rb), matched before any real
    # command and deliberately undocumented: nothing is printed, they are
    # absent from cmd_help, and the animation is the whole response. Matched
    # case-insensitively on the whole line, which submit has already
    # stripped, so only a bare word with no arguments fires one.
    #
    # Returns whether or not the egg actually started: a refused start (one
    # already in flight, or the overlay's canvas failed to allocate) must
    # stay just as silent, rather than falling through to "command not
    # found: dave" and announcing that the word means something.
    if AcidEggs.names.include?(line.downcase)
      AcidEggs.start(line.downcase)
      return
    end
    parts = line.split(" ")
    cmd = parts[0]
    args = parts[1, parts.length - 1] || []
    if cmd == "help"
      cmd_help
    elsif cmd == "clear"
      @lines = []
    elsif cmd == "pwd"
      @lines << @cwd
    elsif cmd == "cd"
      cmd_cd(args)
    elsif cmd == "ls"
      cmd_ls(args)
    elsif cmd == "cat"
      cmd_cat(args)
    elsif cmd == "echo"
      @lines << args.join(" ")
    elsif cmd == "run" || cmd == "open"
      cmd_run(args)
    else
      @lines << "command not found: #{cmd}"
    end
  end

  def cmd_help
    @lines << "help, clear, pwd, cd, ls, cat, echo, run <app>"
  end

  # Resolves a user-typed path (absolute-from-root with a leading "/", or
  # relative to @cwd) into a clean, normalized path that can never climb
  # above ROOT_DIR -- by segment, not by string matching a literal ".."
  # argument. The previous version only special-cased a bare ".." (the
  # whole argument, exactly) and otherwise pasted the raw argument onto
  # @cwd unresolved (`"#{@cwd}/#{arg}"`) -- so anything with an EMBEDDED
  # ".." segment, e.g. `cd foo/../../../..`, sailed straight through to
  # Dir.open/File.open, which the real host filesystem resolves normally,
  # walking this whole simulator process (running with the real user's
  # own file permissions) out of v2/fsroot entirely. Found live during a
  # security audit, not by a user report. Every resolved path is built
  # from a segment list seeded at ROOT_SEGMENTS (for an absolute path) or
  # @cwd's own segments (for a relative one) -- ".." pops one segment but
  # is refused once the list is already down to ROOT_SEGMENTS.length, so
  # there is no string this can ever produce that isn't inside ROOT_DIR.
  # @cwd itself is always the RESULT of a previous resolve_path call, so
  # it's always already clean -- this never has to re-normalize it.
  def resolve_path(arg)
    # No argument at all (bare `cd`) goes to ROOT_DIR, same as a real
    # shell's `cd` with no args going home -- distinct from an explicit
    # "." argument, which below now correctly means "stay in @cwd" (the
    # previous version wrongly treated "." the same as "no argument",
    # sending an explicit `cd .` to ROOT_DIR instead of leaving @cwd
    # alone).
    return ROOT_DIR if arg.nil? || arg == ""
    if arg[0, 1] == "/"
      segments = ROOT_SEGMENTS.dup
      rest = arg[1, arg.length - 1]
    else
      segments = @cwd.split("/")
      rest = arg
    end
    rest.split("/").each do |part|
      next if part == "" || part == "."
      if part == ".."
        segments.pop if segments.length > ROOT_SEGMENTS.length
      else
        segments << part
      end
    end
    segments.join("/")
  end

  def cmd_cd(args)
    target = resolve_path(args[0])
    begin
      d = Dir.open(target)
      d.close
      @cwd = target
    rescue => e
      @lines << "cd: #{args[0]}: #{e.message}"
    end
  end

  def cmd_ls(args)
    target = args.empty? ? @cwd : resolve_path(args[0])
    begin
      d = Dir.open(target)
      names = []
      while (ent = d.read)
        names << ent unless ent == "." || ent == ".."
      end
      d.close
      names.sort.each { |n| @lines << n }
    rescue => e
      @lines << "ls: #{e.message}"
    end
  end

  def cmd_cat(args)
    if args.empty?
      @lines << "cat: missing file"
      return
    end
    path = resolve_path(args[0])
    begin
      f = File.open(path, "r")
      text = f.read
      f.close
      text.split("\n").each { |l| @lines << l }
    rescue => e
      @lines << "cat: #{e.message}"
    end
  end

  def cmd_run(args)
    if args.empty?
      @lines << "run: missing app name"
      return
    end
    query = args[0].downcase
    count = acid_launcher_count
    i = 0
    while i < count
      name = acid_launcher_name(i)
      if name.downcase == query
        acid_launcher_spawn(i)
        return
      end
      i += 1
    end
    @lines << "run: no app named #{args[0]}"
  end

  # While an egg is in flight the loop needs to wake up every frame rather
  # than every 200ms. Typing is unaffected: a keystroke still arrives as an
  # event the moment it happens.
  def poll_timeout_ms
    AcidEggs.active? ? AcidEggs::TICK_MS : 200
  end

  def on_idle
    AcidEggs.step
  end

  def on_destroy
    # Closing the terminal mid-flight takes the overlay with it, rather
    # than leaving a sprite frozen on the screen with nothing left running
    # to clear it.
    AcidEggs.abort
  end
end

TerminalApp.new.start
