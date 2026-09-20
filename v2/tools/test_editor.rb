# Headless tests for the editor's pure modules -- Buffer (editor/buffer.rb)
# and Hl (editor/hl.rb). Neither calls an acid_* binding, so both run under
# the vendored host mruby with no OS underneath them.
#
# Run (this runtime has no require, so the sources are concatenated in,
# exactly the way vm_host loads them into a real app VM):
#
#   cd /home/norfolkh/os && cat v2/apps/editor/buffer.rb v2/apps/editor/hl.rb \
#     v2/tools/test_editor.rb | ./v2/components/mruby/build/host/bin/mruby -

$fails = 0

def eq(actual, expected, what)
  if actual == expected
    puts "  ok  #{what}"
  else
    $fails += 1
    puts "FAIL  #{what}"
    puts "      expected #{expected.inspect}"
    puts "      got      #{actual.inspect}"
  end
end

def group(name)
  puts name
end

# ---------------------------------------------------------------- Buffer

group("Buffer: editing")

b = Buffer.new(["hello"])
b.set_cursor(5, 0)
b.insert_text(" world")
eq(b.lines, ["hello world"], "insert_text appends")
eq([b.cx, b.cy], [11, 0], "cursor lands past inserted text")
eq(b.modified?, true, "insert marks modified")

b = Buffer.new(["hello"])
b.set_cursor(2, 0)
b.insert_text("\n")
eq(b.lines, ["he", "llo"], "newline splits the line")
eq([b.cx, b.cy], [0, 1], "cursor moves to start of new line")

b = Buffer.new(["one", "two"])
b.set_cursor(3, 0)
b.delete_forward
eq(b.lines, ["onetwo"], "delete at end of line joins the next")

b = Buffer.new(["one", "two"])
b.set_cursor(0, 1)
b.backspace
eq(b.lines, ["onetwo"], "backspace at start of line joins the previous")
eq([b.cx, b.cy], [3, 0], "cursor sits at the join")

b = Buffer.new(["abc", "def", "ghi"])
b.delete_range(1, 0, 2, 2)
eq(b.lines, ["ai"], "delete_range spanning lines collapses them")
eq([b.cx, b.cy], [1, 0], "cursor lands at the range start")

b = Buffer.new(["ab"])
b.set_cursor(1, 0)
b.insert_text("X\nY")
eq(b.lines, ["aX", "Yb"], "multi-line insert splits around the cursor")
eq([b.cx, b.cy], [1, 1], "cursor lands past multi-line insert")

group("Buffer: cursor")

b = Buffer.new(["abc", "de"])
b.set_cursor(3, 0)
b.move(1, 0)
eq([b.cx, b.cy], [0, 1], "right at end of line wraps to the next")
b.move(-1, 0)
eq([b.cx, b.cy], [3, 0], "left at start of line wraps to the previous")
b.set_cursor(0, 0)
b.move(-1, 0)
eq([b.cx, b.cy], [0, 0], "left at start of buffer stays put")
b.set_cursor(3, 0)
b.move(0, 1)
eq([b.cx, b.cy], [2, 1], "down onto a shorter line clamps the column")
b.set_cursor(0, 99)
eq(b.cy, 1, "set_cursor clamps past the last line")

group("Buffer: undo/redo")

b = Buffer.new([""])
"word".split("").each { |c| b.insert_char(c) }
eq(b.lines, ["word"], "typed characters land")
eq(b.undo, true, "undo reports it did something")
eq(b.lines, [""], "a typed run undoes as one step")

b = Buffer.new([""])
"ab cd".split("").each { |c| b.insert_char(c) }
eq(b.lines, ["ab cd"], "typed run with a space lands")
b.undo
eq(b.lines, ["ab "], "undo steps back one word, not the whole line")
b.undo
eq(b.lines, ["ab"], "the space is its own step")
b.undo
eq(b.lines, [""], "and the first word is another")

b = Buffer.new([""])
"hi".split("").each { |c| b.insert_char(c) }
b.undo
eq(b.redo, true, "redo reports it did something")
eq(b.lines, ["hi"], "redo reapplies the undone run")
b.undo
b.insert_char("x")
eq(b.redo, false, "a new edit invalidates redo")

b = Buffer.new(["abc"])
eq(b.undo, false, "undo on an untouched buffer is a no-op")

b = Buffer.new(["one", "two"])
b.set_cursor(3, 0)
b.delete_forward
b.undo
eq(b.lines, ["one", "two"], "undo restores a joined line")

b = Buffer.new([""])
n = 0
while n < Buffer::UNDO_MAX + 20
  b.insert_char("x")
  b.end_group
  n += 1
end
n = 0
n += 1 while b.undo
eq(n, Buffer::UNDO_MAX, "the undo stack caps at UNDO_MAX records")

group("Buffer: dirty lines")

b = Buffer.new(["a", "b", "c"])
b.take_dirty
b.set_cursor(1, 1)
b.insert_char("x")
eq(b.take_dirty, [1], "a single-line edit dirties only that line")
b.insert_text("\n")
eq(b.take_dirty, :all, "a line-count change dirties everything")

group("Buffer: selection")

b = Buffer.new(["abcd"])
b.set_cursor(1, 0)
b.toggle_mark
b.set_cursor(3, 0)
eq(b.selection_range, [1, 0, 3, 0], "mark before cursor")
eq(b.selected_text, "bc", "selected text on one line")

b = Buffer.new(["abcd"])
b.set_cursor(3, 0)
b.toggle_mark
b.set_cursor(1, 0)
eq(b.selection_range, [1, 0, 3, 0], "mark after cursor normalises")
eq(b.selected_text, "bc", "selected text is the same either way")

b = Buffer.new(["one", "two", "three"])
b.set_cursor(1, 0)
b.toggle_mark
b.set_cursor(2, 2)
eq(b.selected_text, "ne\ntwo\nth", "selection spans lines")

b = Buffer.new(["abc"])
b.set_cursor(1, 0)
b.toggle_mark
eq(b.selection_range, nil, "an empty selection is no selection")
b.toggle_mark
eq(b.mark_set?, false, "toggle_mark clears an existing mark")

group("Buffer: clipboard")

b = Buffer.new(["hello world"])
b.set_cursor(0, 0)
b.toggle_mark
b.set_cursor(5, 0)
eq(b.copy, true, "copy reports success")
eq(b.clipboard, "hello", "copy takes the selected text")
eq(b.lines, ["hello world"], "copy leaves the buffer alone")

b = Buffer.new(["hello world"])
b.set_cursor(0, 0)
b.toggle_mark
b.set_cursor(6, 0)
eq(b.cut, true, "cut reports success")
eq(b.lines, ["world"], "cut removes the selection")
eq(b.mark_set?, false, "cut clears the mark")
b.undo
eq(b.lines, ["hello world"], "cut undoes as one step")

b = Buffer.new(["ab"])
b.set_cursor(2, 0)
b.toggle_mark
b.set_cursor(0, 0)
b.cut
b.set_cursor(0, 0)
eq(b.paste, true, "paste reports success")
eq(b.lines, ["ab"], "paste puts it back")

b = Buffer.new(["xy"])
b.set_cursor(1, 0)
eq(b.paste, false, "paste with an empty clipboard is a no-op")

b = Buffer.new(["one", "two"])
b.set_cursor(0, 0)
b.toggle_mark
b.set_cursor(3, 1)
b.cut
eq(b.lines, [""], "cutting everything leaves one empty line")
b.set_cursor(0, 0)
b.paste
eq(b.lines, ["one", "two"], "pasting multi-line text restores the lines")

group("Buffer: find")

b = Buffer.new(["alpha beta", "gamma", "beta delta"])
eq(b.find("beta", 0, 0), [6, 0], "find forward on the first line")
eq(b.find("beta", 7, 0), [0, 2], "find continues onto later lines")
eq(b.find("alpha", 0, 2), [0, 0], "find wraps to the top")
eq(b.find("zzz", 0, 0), nil, "find reports no match")
eq(b.find("", 0, 0), nil, "find on an empty query is nil")
eq(b.find("beta", 1, 0), [6, 0], "find matches later on the cursor's own line")

group("Buffer: paste over selection (two-record behaviour)")

# Pasting over an active selection takes two undo records: one for
# delete_selection, one for insert_text. This is intentional: after one undo,
# the buffer is in the post-delete state, which is reachable by pressing
# Delete alone, so it is a coherent state, not corrupt.
b = Buffer.new(["hello world"])
b.set_cursor(0, 0)
b.toggle_mark
b.set_cursor(5, 0)
b.copy
b.clear_mark
b.set_cursor(6, 0)
b.toggle_mark
b.set_cursor(11, 0)
b.paste
eq(b.lines, ["hello hello"], "paste replaces the selection")
b.undo
eq(b.lines, ["hello "], "undo of a replacing paste steps back to the post-delete state")
b.undo
eq(b.lines, ["hello world"], "a second undo restores the replaced text")

group("Buffer: stale mark coordinates")

# When the buffer mutates elsewhere, mark coordinates become stale.
# Clamping at read time prevents selected_text and delete_selection from
# producing nil or corrupting the buffer.

# Reproduction 1: backspace shrinks the line the mark sits on
b = Buffer.new(["short", "target line here", "third"])
b.set_cursor(16, 1)
b.toggle_mark
b.set_cursor(10, 1)
10.times { b.backspace }
# Mark is stale at (16, 1), line 1 now has 6 chars ("e here")
# After clamping mark_x to 6: selection is [0, 1, 6, 1] = "e here"
selected = b.selected_text
eq(selected.nil?, false, "selected_text with stale mark is not nil")
eq(selected, "e here", "selected_text clamping gives correct range")
eq(b.delete_selection, true, "delete_selection with stale mark succeeds")
eq(b.lines, ["short", "", "third"], "delete_selection with stale mark removes clamped range")
b.undo
eq(b.lines, ["short", "e here", "third"], "undo of delete with stale mark restores")

# Reproduction 2: insert before the mark
b = Buffer.new(["hello world"])
b.set_cursor(6, 0)
b.toggle_mark
b.set_cursor(0, 0)
b.insert_text("XXX ")
# Mark is stale at (6, 0), cursor at (4, 0), line is now "XXX hello world"
# Clamped mark is (6, 0), so selection is [4, 0, 6, 0] = "he"
selected = b.selected_text
eq(selected.nil?, false, "selected_text after insert-before-mark is not nil")
eq(selected, "he", "selected_text clamping gives in-range result")

# Reproduction 3: mark left past the end of the buffer
b = Buffer.new(["line 1", "line 2", "line 3"])
b.set_cursor(6, 2)
b.toggle_mark
b.set_cursor(0, 0)
# Remove lines 1 and 2, leaving just "line 3", but don't update the mark
b.delete_range(0, 0, 0, 2)
eq(b.lines, ["line 3"], "deleted lines 0-1")
# Mark is stale at (6, 2), but only line 0 exists now
# After clamping: mark_y clamps to 0, mark_x stays 6 (line length is 6)
# Cursor is at (0, 0), so selection is [0, 0, 6, 0]
r = b.selection_range
eq(r.nil?, false, "selection_range with out-of-range mark_y clamped and valid")
selected = b.selected_text
eq(selected.nil?, false, "selected_text with out-of-range mark_y is not nil")
eq(selected, "line 3", "selected_text covers the clamped range")
b.undo
eq(b.lines, ["line 1", "line 2", "line 3"], "undo restores the deleted lines")

# ----------------------------------------------------------------- done

group("Hl: tokenizer")

def toks(line)
  Hl.tokenize(line).map { |t| [t[0], t[1]] }
end

def colors_of(line, word)
  Hl.tokenize(line).each { |t| return t[1] if t[0] == word }
  nil
end

eq(Hl.tokenize("").length, 0, "an empty line has no tokens")

eq(Hl.tokenize("abc").map { |t| t[0] }.join(""), "abc",
   "tokens cover the whole line")
eq(Hl.tokenize("x = foo(1, :bar) # note").map { |t| t[0] }.join(""),
   "x = foo(1, :bar) # note", "tokens cover a busy line exactly")

eq(colors_of("def hi", "def"), Hl::KEYWORD, "def is a keyword")
eq(colors_of("ending = 1", "ending"), Hl::PLAIN,
   "a keyword inside an identifier is not a keyword")
eq(colors_of("x.end", "end"), Hl::KEYWORD, "end after a dot still reads as one")

eq(colors_of("s = \"hi\"", "\"hi\""), Hl::STRING, "double-quoted string")
eq(colors_of("s = 'hi'", "'hi'"), Hl::STRING, "single-quoted string")
eq(colors_of("s = \"a#b\"", "\"a#b\""), Hl::STRING,
   "a # inside a string does not start a comment")
eq(colors_of("s = \"a\\\"b\"", "\"a\\\"b\""), Hl::STRING,
   "an escaped quote does not end the string")

eq(colors_of("x # note", "# note"), Hl::COMMENT, "comment to end of line")
eq(colors_of("# whole", "# whole"), Hl::COMMENT, "whole-line comment")

eq(colors_of("x = :sym", ":sym"), Hl::SYMBOL, "symbol")
eq(colors_of("A::B", "::"), Hl::PLAIN, ":: is not a symbol")
eq(colors_of("A::B", "A"), Hl::SYMBOL, "a constant shares the symbol colour")
eq(colors_of("AcidKeys::ESCAPE", "ESCAPE"), Hl::SYMBOL,
   "the name after :: is a constant, not a symbol")

eq(colors_of("@ivar = 1", "@ivar"), Hl::IVAR, "instance variable")
eq(colors_of("x = 42", "42"), Hl::NUMBER, "number")
eq(colors_of("x = 0xFF66", "0xFF66"), Hl::NUMBER, "hex literal reads as one number")
eq(colors_of("1.upto(3)", "1"), Hl::NUMBER,
   "a number before a method call does not swallow the dot")
eq(colors_of("x = 1.5", "1.5"), Hl::NUMBER, "a decimal keeps its point")

raise "#{$fails} failure(s)" if $fails > 0
puts "all passed"
