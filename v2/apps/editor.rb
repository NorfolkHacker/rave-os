class EditorApp < AcidApp
  include EditorLayout
  include EditorCmd
  include EditorTouch

  DEFAULT_FILE = "v2/fsroot/Home/notes.txt"

  BG_COLOR = 0x0B1712      # THEME_PANEL -- status line
  BODY_BG = 0x050607       # THEME_BG -- text body
  GUTTER_BG = 0x050607     # THEME_BG
  GUTTER_COLOR = 0x9DAAA3  # THEME_MUTED
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  CURSOR_COLOR = 0x00FF66  # THEME_HARD
  STATUS_COLOR = 0x9DAAA3  # THEME_MUTED

  def on_create
    # File Manager launches this with a specific file to view/edit
    # (clicking a .rb file spawns Editor with that path as the launch
    # arg -- see file_manager.rb's activate_selected); opened directly
    # from Menu with no arg, it falls back to the general notes file it
    # always edited before. Either way the file is genuinely editable
    # and saveable, including files under fsroot/App (a real symlink to
    # v2/apps) -- there's no separate read-only mode, by design: this is
    # meant to double as a live way to tweak an app's own source and see
    # the change on its next launch, no rebuild step.
    arg = acid_launch_arg
    @path = arg.empty? ? DEFAULT_FILE : arg
    @buf = Buffer.new(read_lines)
    @scroll_y = 0
    @scroll_x = 0
    @message = nil
    # On for Ruby, off for anything else -- a .txt file has no syntax to
    # show and colouring prose at random is worse than leaving it alone.
    # ESC h overrides it for this window.
    @hl_on = @path.end_with?(".rb")
    @hl_cache = []
  end

  def read_lines
    f = File.open(@path, "r")
    text = f.read
    f.close
    text.split("\n")
  rescue
    [""]
  end

  # Sibling temp path, not touching @path until the new content is fully
  # written and closed. File.open(@path, "w") truncates the target the
  # instant it succeeds -- so a write that fails partway (a full disk, a
  # yanked SD card on the hw target, anything) used to leave the original
  # file gone, not merely unsaved, and with no way back in from inside
  # this OS (see this task's brief). Writing to a sibling first and
  # File.rename-ing it over @path only if that write fully succeeds means
  # a failure before the rename leaves the original byte-for-byte
  # untouched. The suffix is one no real file is likely to already be
  # using -- @path itself, mid-edit in this very window, is the one path
  # a plain ".tmp" or "~" convention risks colliding with.
  SAVE_TMP_SUFFIX = ".editor-save-tmp"

  def save_file
    backup_failed = !backup_own_source
    tmp = @path + SAVE_TMP_SUFFIX
    wrote = false
    f = nil
    begin
      f = File.open(tmp, "w")
      # Trailing newline, not just lines joined by one -- POSIX text files
      # end in one, and this app regularly saves real source files under
      # fsroot/App (the live v2/apps symlink): saving without it was
      # confirmed live to strip an existing app.rb's final newline on
      # every save, which is diff noise against git history for no
      # reason.
      f.write(@buf.lines.join("\n") + "\n")
      wrote = true
    rescue
      wrote = false
    end
    # Close on every path, including failure -- the old code never closed
    # f once the write raised, leaking a descriptor on top of losing data.
    f.close if f
    saved = false
    if wrote
      begin
        File.rename(tmp, @path)
        saved = true
      rescue
        saved = false
      end
    end
    # A half-written temp file (write failed) or a temp file the rename
    # couldn't place (rename failed) is debris either way -- clean it up
    # rather than leaving it for the user to find later. Best-effort: if
    # even this fails there is nothing more useful to do about it.
    unless saved
      begin
        File.delete(tmp) if File.exist?(tmp)
      rescue
      end
    end
    if saved
      @buf.mark_saved
      @message = backup_failed ? "saved (backup failed)" : "saved"
    else
      @message = "save failed"
    end
    saved
  end

  # Only for the files listed in EditorLayout::OWN_SOURCE_ROOTS /
  # OWN_SOURCE_RELATIVE_PATHS -- the
  # user chose this scope explicitly over backing up every save, since
  # this app is the one editor that can edit and then immediately re-run
  # the very code it's running as. Copies the CURRENT on-disk contents
  # (not the buffer -- the buffer is what's about to overwrite it) to
  # "<path>.bak" before that happens. A failure here (missing file on a
  # first save, an unwritable sibling, anything) must never block the
  # real save -- a user who can't save at all is worse off than one whose
  # backup didn't take -- so this always returns rather than raising, and
  # save_file only uses the result to add a note to @message.
  def backup_own_source
    return true unless own_source?(@path)
    # A first save of a brand new own-source file has nothing to back up
    # -- that's not a backup failure worth a "(backup failed)" note next
    # to "saved", it's just the expected shape of creating something new.
    return true unless File.exist?(@path)
    current = nil
    f = nil
    begin
      f = File.open(@path, "r")
      current = f.read
    rescue
      current = nil
    end
    f.close if f
    return false if current.nil?
    out = nil
    ok = false
    begin
      out = File.open(@path + ".bak", "w")
      out.write(current)
      ok = true
    rescue
      ok = false
    end
    out.close if out
    ok
  end

  def visible_lines
    (STATUS_Y - TEXT_Y) / LINE_H
  end

  def visible_cols
    (WINDOW_W - TEXT_X) / CHAR_W
  end

  def file_label
    slash = @path.rindex("/")
    slash ? @path[slash + 1, @path.length - slash - 1] : @path
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    draw_gutter
    draw_lines
    draw_cursor
    draw_cmd_strip if cmd_active?
    if cmd_prompt_active?
      draw_cmd_prompt
    else
      draw_status
    end
    acid_draw_window_border
  end

  def draw_status
    acid_fill_rect(0, STATUS_Y, WINDOW_W, LINE_H, BG_COLOR)
    left = @message ? @message : (file_label + (@buf.modified? ? " *" : ""))
    right = "#{@buf.cy + 1},#{@buf.cx + 1}  #{@buf.line_count}L#{@hl_on ? '  hl' : ''}"
    # 28 was right for the old 240px window (35 columns total); at 420px
    # (70 columns) the right-hand field only ever needs ~14-23 of them
    # (see right, above -- even a 4-digit cursor position/line count plus
    # "  hl" is 20 chars), so 28 was clipping real messages mid-word, e.g.
    # "unsaved -- ESC q again to close" (31 chars) lost its last word.
    # 48 leaves the right field a comfortable margin: left[0,48] ends at
    # pixel 2+48*6=290, and `right` only reaches x=298 (its start pixel)
    # at a 4-digit cursor row/col or line count -- an 8px gap -- and stays
    # clear at any line count this editor is actually used at (nothing in
    # this codebase's own source, the largest realistic file it edits,
    # tops even 3 digits). Only a 5-digit line count (99999+) would ever
    # collide, which is not a real file size here.
    acid_draw_text(left[0, 48], 2, STATUS_Y + 1, STATUS_COLOR, BG_COLOR)
    acid_draw_text(right, WINDOW_W - right.length * CHAR_W - 2, STATUS_Y + 1,
                   STATUS_COLOR, BG_COLOR)
  end

  def draw_gutter
    acid_fill_rect(0, TEXT_Y, GUTTER_W, STATUS_Y - TEXT_Y, GUTTER_BG)
    i = 0
    while i < visible_lines
      idx = @scroll_y + i
      break if idx >= @buf.line_count
      num = (idx + 1).to_s
      acid_draw_text(num, GUTTER_W - num.length * CHAR_W - 2,
                     TEXT_Y + i * LINE_H + 1, GUTTER_COLOR, GUTTER_BG)
      i += 1
    end
  end

  SEL_BG = 0x123322    # THEME_PANEL's documented button-hover shade,
                       # the same highlight file_manager.rb uses for its
                       # selected row

  def draw_lines
    hl_invalidate
    i = 0
    while i < visible_lines
      idx = @scroll_y + i
      y = TEXT_Y + i * LINE_H
      acid_fill_rect(TEXT_X, y, WINDOW_W - TEXT_X, LINE_H, BODY_BG)
      sel = selection_span(idx)
      unless sel.nil?
        sx = sel[0] - @scroll_x
        ex = sel[1] - @scroll_x
        sx = 0 if sx < 0
        ex = visible_cols if ex > visible_cols
        acid_fill_rect(TEXT_X + sx * CHAR_W, y, (ex - sx) * CHAR_W, LINE_H, SEL_BG) if ex > sx
      end
      if idx < @buf.line_count
        if @hl_on
          draw_hl_line(idx, y)
        else
          visible_text = @buf.line(idx)[@scroll_x, visible_cols] || ""
          acid_draw_text(visible_text, TEXT_X, y + 1, TEXT_COLOR, BODY_BG)
        end
      end
      i += 1
    end
  end

  # Tokens for one line, tokenized on first sight and kept until that
  # line changes. Buffer reports what went stale (take_dirty); :all means
  # the line count itself moved, so every cached index past the edit is
  # wrong and the cheapest correct answer is to start over.
  def hl_tokens(index)
    cached = @hl_cache[index]
    return cached unless cached.nil?
    toks = Hl.tokenize(@buf.line(index))
    @hl_cache[index] = toks
    toks
  end

  # Runs once per redraw, from draw_lines, so it must fire even when
  # highlighting is off -- otherwise a buffer edited with colour disabled
  # keeps stale tokens once it's switched back on. take_dirty is
  # read-and-clear, so calling this again from hl_tokens (once per line,
  # per the brief) would always see an empty result after the first line
  # -- dead work in a per-line loop -- which is why it lives here only.
  def hl_invalidate
    dirty = @buf.take_dirty
    return if dirty == []
    if dirty == :all
      @hl_cache = []
      return
    end
    dirty.each { |i| @hl_cache[i] = nil }
  end

  def draw_hl_line(index, y)
    col = 0
    limit = @scroll_x + visible_cols
    hl_tokens(index).each do |t|
      text = t[0]
      start_col = col
      col += text.length
      next if col <= @scroll_x
      break if start_col >= limit
      cut = @scroll_x - start_col
      cut = 0 if cut < 0
      vis = text[cut, text.length - cut]
      screen_col = start_col + cut - @scroll_x
      room = visible_cols - screen_col
      vis = vis[0, room] if vis.length > room
      acid_draw_text(vis, TEXT_X + screen_col * CHAR_W, y + 1, t[1], BODY_BG)
    end
  end

  # The [start_col, end_col] of the selection on one line, or nil. A line
  # fully inside a multi-line selection runs to its own length plus one,
  # so the newline it swallowed is visible as a highlighted cell rather
  # than the selection appearing to stop short at the end of the text.
  def selection_span(index)
    r = @buf.selection_range
    return nil if r.nil?
    sx, sy, ex, ey = r
    return nil if index < sy || index > ey
    from = (index == sy) ? sx : 0
    to = (index == ey) ? ex : @buf.line(index).length + 1
    return nil if to <= from
    [from, to]
  end

  def draw_cursor
    row = @buf.cy - @scroll_y
    return if row < 0 || row >= visible_lines
    col = @buf.cx - @scroll_x
    return if col < 0 || col >= visible_cols
    x = TEXT_X + col * CHAR_W
    y = TEXT_Y + row * LINE_H
    acid_fill_rect(x, y + LINE_H - 2, CHAR_W, 2, CURSOR_COLOR)
  end

  def on_touch(x, y, pressed)
    @message = nil if pressed
    redraw if editor_touch(x, y, pressed)
  end

  def on_key(code, pressed)
    return unless pressed
    return redraw if cmd_prompt_key(code)
    return redraw if cmd_key(code)
    @message = nil
    # Not on ESCAPE: this is the ESCAPE that reopens the strip after "q"
    # auto-closed it (cmd_key's own ESCAPE branch handles the cancel
    # case, where the strip was already open) -- see cmd_key's comment.
    @quit_armed = false unless code == AcidKeys::ESCAPE
    if code == AcidKeys::ESCAPE
      cmd_open
      return redraw
    elsif code == AcidKeys::UP
      @buf.move(0, -1)
    elsif code == AcidKeys::DOWN
      @buf.move(0, 1)
    elsif code == AcidKeys::LEFT
      @buf.move(-1, 0)
    elsif code == AcidKeys::RIGHT
      @buf.move(1, 0)
    elsif code == AcidKeys::ENTER
      @buf.split_line
    elsif code == AcidKeys::BACKSPACE
      @buf.backspace
    elsif code == AcidKeys::DELETE
      @buf.delete_forward
    elsif code == AcidKeys::TAB
      # Two spaces, not a tab character: every width calculation in this
      # app counts characters, and a literal tab would make the cursor
      # column and the drawn column disagree from that point on.
      @buf.insert_text("  ")
    elsif code >= 32 && code <= 126
      @buf.insert_char(code.chr)
    end
    ensure_scroll
    redraw
  end

  def ensure_scroll
    if @buf.cy < @scroll_y
      @scroll_y = @buf.cy
    elsif @buf.cy >= @scroll_y + visible_lines
      @scroll_y = @buf.cy - visible_lines + 1
    end
    if @buf.cx < @scroll_x
      @scroll_x = @buf.cx
    elsif @buf.cx >= @scroll_x + visible_cols
      @scroll_x = @buf.cx - visible_cols + 1
    end
  end
end

EditorApp.new.start
