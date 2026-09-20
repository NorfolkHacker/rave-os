# The editor's text buffer: the lines, the cursor, every mutation, and
# undo/redo. Selection and the clipboard arrive in Task 4.
#
# Calls no acid_* binding on purpose -- it is arrays and strings and
# nothing else, so v2/tools/test_editor.rb runs it under the host mruby
# with no OS underneath it. Everything that needs to draw lives in
# editor.rb.
#
# Every mutation goes through exactly two primitives: insert text at a
# position, and delete a range. A newline is just text, so splitting and
# joining lines are not separate operations, and undo has two record
# types to invert rather than six.
class Buffer
  # Deliberately a cap on records, not on bytes: an app VM runs in a fixed
  # mruby pool (vm_host.c logs its usage), and an unbounded history in a
  # long editing session is the kind of slow leak that shows up as a
  # mysterious allocation failure hours later.
  UNDO_MAX = 200

  attr_reader :cx, :cy, :clipboard

  def initialize(lines)
    @lines = (lines.nil? || lines.empty?) ? [""] : lines
    @cx = 0
    @cy = 0
    @undo = []
    @redo = []
    @modified = false
    # True once something has closed the current typing run, so the next
    # inserted character starts a fresh undo record instead of joining
    # the previous one. See insert_char.
    @group_closed = false
    @dirty = []
    @dirty_all = true
    @mark_x = nil
    @mark_y = nil
    @clipboard = ""
  end

  def lines
    @lines
  end

  def line_count
    @lines.length
  end

  def line(i)
    @lines[i] || ""
  end

  def current_line
    line(@cy)
  end

  def modified?
    @modified
  end

  # Called after a successful save: the text is unchanged, but it is no
  # longer different from what's on disk.
  def mark_saved
    @modified = false
  end

  # ---- cursor ----

  def set_cursor(x, y)
    y = 0 if y < 0
    y = @lines.length - 1 if y >= @lines.length
    x = 0 if x < 0
    x = line(y).length if x > line(y).length
    @cx = x
    @cy = y
    end_group
  end

  def move(dx, dy)
    if dy != 0
      ny = @cy + dy
      ny = 0 if ny < 0
      ny = @lines.length - 1 if ny >= @lines.length
      @cy = ny
      # A shorter line can't hold the old column; clamping rather than
      # remembering the "desired" column keeps this to one rule, and the
      # window is wide enough that the difference rarely shows.
      @cx = current_line.length if @cx > current_line.length
    end
    if dx != 0
      @cx += dx
      if @cx < 0
        if @cy > 0
          @cy -= 1
          @cx = current_line.length
        else
          @cx = 0
        end
      elsif @cx > current_line.length
        if @cy < @lines.length - 1
          @cy += 1
          @cx = 0
        else
          @cx = current_line.length
        end
      end
    end
    end_group
  end

  # ---- primitives (no undo record: the wrappers below own that, so undo
  #      itself can use these to put text back without recording the
  #      put-back as a fresh edit) ----

  def raw_insert(x, y, text)
    return [x, y] if text.nil? || text.length == 0
    parts = text.split("\n", -1)
    parts = [""] if parts.empty?
    src = line(y)
    head = src[0, x]
    tail = src[x, src.length - x]
    if parts.length == 1
      @lines[y] = head + parts[0] + tail
      mark_dirty(y)
      return [x + parts[0].length, y]
    end
    @lines[y] = head + parts[0]
    i = 1
    while i < parts.length - 1
      @lines.insert(y + i, parts[i])
      i += 1
    end
    last = parts[parts.length - 1]
    @lines.insert(y + parts.length - 1, last + tail)
    mark_dirty_all
    [last.length, y + parts.length - 1]
  end

  # Start must not come after end. Returns the text removed, so a caller
  # can record it for undo.
  def raw_delete(sx, sy, ex, ey)
    if sy == ey
      src = line(sy)
      text = src[sx, ex - sx]
      @lines[sy] = src[0, sx] + src[ex, src.length - ex]
      mark_dirty(sy)
      return text
    end
    parts = [line(sy)[sx, line(sy).length - sx]]
    i = sy + 1
    while i < ey
      parts << line(i)
      i += 1
    end
    parts << line(ey)[0, ex]
    @lines[sy] = line(sy)[0, sx] + line(ey)[ex, line(ey).length - ex]
    i = ey
    while i > sy
      @lines.delete_at(i)
      i -= 1
    end
    mark_dirty_all
    parts.join("\n")
  end

  # ---- recording edits ----

  def insert_text(text)
    return if text.nil? || text.length == 0
    bx = @cx
    by = @cy
    ex, ey = raw_insert(bx, by, text)
    push_undo([:ins, bx, by, text, bx, by])
    @cx = ex
    @cy = ey
    @modified = true
  end

  def delete_range(sx, sy, ex, ey)
    text = raw_delete(sx, sy, ex, ey)
    push_undo([:del, sx, sy, text, @cx, @cy])
    @cx = sx
    @cy = sy
    @modified = true
    text
  end

  # A run of typed non-space characters coalesces into one undo record, so
  # undo steps back by word rather than by letter -- the difference
  # between undo being useful and being a way to watch your own typing in
  # reverse. A space, a newline, a cursor move or any other kind of edit
  # ends the run.
  def insert_char(ch)
    if ch != " " && coalescable?
      raw_insert(@cx, @cy, ch)
      rec = @undo[@undo.length - 1]
      rec[3] = rec[3] + ch
      @cx += 1
      @modified = true
      @redo = []
      return
    end
    insert_text(ch)
    end_group if ch == " "
  end

  def split_line
    insert_text("\n")
    end_group
  end

  def backspace
    return if @cx == 0 && @cy == 0
    if @cx > 0
      delete_range(@cx - 1, @cy, @cx, @cy)
    else
      prev_len = line(@cy - 1).length
      delete_range(prev_len, @cy - 1, 0, @cy)
    end
    end_group
  end

  def delete_forward
    if @cx < current_line.length
      delete_range(@cx, @cy, @cx + 1, @cy)
    elsif @cy < @lines.length - 1
      delete_range(@cx, @cy, 0, @cy + 1)
    end
    end_group
  end

  # ---- undo ----

  def end_group
    @group_closed = true
  end

  def undo
    rec = @undo.pop
    return false if rec.nil?
    apply(rec, true)
    @redo.push(rec)
    end_group
    true
  end

  def redo
    rec = @redo.pop
    return false if rec.nil?
    apply(rec, false)
    @undo.push(rec)
    end_group
    true
  end

  # ---- highlight cache support ----

  # Which lines' cached tokens went stale since the last call, as a list of
  # indexes or :all when the line count itself changed (every index past
  # the edit shifted, so a list would have to name most of the file
  # anyway). Clears as it reports, the same read-and-clear shape
  # gfx_take_dirty uses in C.
  def take_dirty
    return_all = @dirty_all
    out = return_all ? :all : @dirty
    @dirty = []
    @dirty_all = false
    out
  end

  # ---- selection ----
  #
  # A mark, not shift-and-arrow: Shift is resolved into the character at
  # translate time (hal_input_sim.cpp), so a shifted arrow is
  # indistinguishable from a plain one and shift-selection cannot be
  # implemented at all here. Setting a mark and then moving is the same
  # idea reached by the one road that's open.

  def mark_set?
    !@mark_y.nil?
  end

  def toggle_mark
    if mark_set?
      clear_mark
    else
      @mark_x = @cx
      @mark_y = @cy
    end
  end

  def clear_mark
    @mark_x = nil
    @mark_y = nil
  end

  # [sx, sy, ex, ey] in document order, or nil when there's no mark or the
  # mark is exactly on the cursor -- so no caller has to ask which end
  # came first, and none has to special-case a zero-width span.
  def selection_range
    return nil unless mark_set?
    return nil if @mark_x == @cx && @mark_y == @cy
    if @mark_y < @cy || (@mark_y == @cy && @mark_x < @cx)
      [@mark_x, @mark_y, @cx, @cy]
    else
      [@cx, @cy, @mark_x, @mark_y]
    end
  end

  def selected_text
    r = selection_range
    return nil if r.nil?
    sx, sy, ex, ey = r
    return line(sy)[sx, ex - sx] if sy == ey
    parts = [line(sy)[sx, line(sy).length - sx]]
    i = sy + 1
    while i < ey
      parts << line(i)
      i += 1
    end
    parts << line(ey)[0, ex]
    parts.join("\n")
  end

  def delete_selection
    r = selection_range
    return false if r.nil?
    sx, sy, ex, ey = r
    clear_mark
    delete_range(sx, sy, ex, ey)
    end_group
    true
  end

  # ---- clipboard ----
  #
  # App-local. A clipboard shared with the Terminal and File Manager would
  # be a kernel service with its own ownership and lifetime questions;
  # this is the version that earns its keep today.

  def copy
    t = selected_text
    return false if t.nil?
    @clipboard = t
    true
  end

  def cut
    return false unless copy
    delete_selection
  end

  def paste
    return false if @clipboard.nil? || @clipboard.length == 0
    delete_selection if mark_set?
    insert_text(@clipboard)
    end_group
    true
  end

  # ---- find ----

  # Searches forward from (from_x, from_y), wrapping to the top of the
  # buffer exactly once, and returns [x, y] or nil. It wraps because a
  # query that only appears above the cursor still has to be findable --
  # scanning to the end and stopping would report "not found" for text
  # plainly on screen.
  def find(query, from_x, from_y)
    return nil if query.nil? || query.length == 0
    n = @lines.length
    i = 0
    while i <= n
      y = (from_y + i) % n
      start = (i == 0) ? from_x : 0
      hit = line(y).index(query, start)
      return [hit, y] unless hit.nil?
      i += 1
    end
    nil
  end

  private

  def coalescable?
    return false if @group_closed
    rec = @undo[@undo.length - 1]
    return false if rec.nil?
    return false unless rec[0] == :ins
    return false unless rec[3].index("\n").nil?
    rec[2] == @cy && rec[1] + rec[3].length == @cx
  end

  def push_undo(rec)
    @undo.push(rec)
    @undo.shift if @undo.length > UNDO_MAX
    @redo = []
    @group_closed = false
  end

  def apply(rec, inverse)
    type, x, y, text, cx, cy = rec
    insert = (type == :ins) ? !inverse : inverse
    if insert
      raw_insert(x, y, text)
    else
      ex, ey = end_of(x, y, text)
      raw_delete(x, y, ex, ey)
    end
    if inverse
      set_cursor(cx, cy)
    elsif type == :ins
      ex, ey = end_of(x, y, text)
      set_cursor(ex, ey)
    else
      set_cursor(x, y)
    end
    @modified = true
  end

  def end_of(x, y, text)
    parts = text.split("\n", -1)
    return [x, y] if parts.empty?
    return [x + text.length, y] if parts.length == 1
    [parts[parts.length - 1].length, y + parts.length - 1]
  end

  def mark_dirty(i)
    @dirty << i unless @dirty.include?(i)
  end

  def mark_dirty_all
    @dirty_all = true
  end
end
