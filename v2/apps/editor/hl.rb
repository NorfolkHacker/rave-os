# Ruby syntax highlighting for the editor: one line in, a list of
# [text, color] runs out, covering the line in order with nothing
# dropped.
#
# Per line on purpose. An edit then invalidates exactly one cache entry
# (Buffer#take_dirty), which is what keeps redraw cheap while typing. The
# cost is that multi-line strings, heredocs and =begin blocks are not
# understood -- getting those right means re-tokenizing from the top of
# the file on every keystroke, which is the wrong trade for the payoff.
# See the design doc.
#
# Calls no acid_* binding, so v2/tools/test_editor.rb runs it headless.
module Hl
  # Not kernel_theme.h's five chrome colours -- the wallpaper's own neon
  # palette (wallpaper_data.h), so highlighted source reads as part of
  # this OS rather than as a generic editor theme dropped into it.
  KEYWORD = 0xFF2D78   # neon pink
  STRING  = 0xFFD400   # yellow
  NUMBER  = 0x00E5FF   # cyan
  SYMBOL  = 0xB026FF   # violet -- THEME_VIOLET, shared with constants
  IVAR    = 0xFF7A00   # orange
  COMMENT = 0x9DAAA3   # THEME_MUTED
  PLAIN   = 0xD4E6DB   # THEME_TEXT

  KEYWORDS = ["def", "end", "class", "module", "if", "elsif", "else",
              "unless", "while", "until", "do", "return", "yield", "nil",
              "true", "false", "self", "and", "or", "not", "begin",
              "rescue", "ensure", "case", "when", "then", "next", "break",
              "attr_reader", "attr_accessor", "require", "include"]

  def self.tokenize(line)
    out = []
    i = 0
    n = line.length
    while i < n
      ch = line[i, 1]
      if ch == "#"
        out << [line[i, n - i], COMMENT]
        i = n
      elsif ch == "\"" || ch == "'"
        stop = string_end(line, i, ch, n)
        out << [line[i, stop - i], STRING]
        i = stop
      elsif ch == ":" && line[i + 1, 1] == ":"
        # Scope resolution, not a symbol. Without this the ":B" of
        # "AcidKeys::ESCAPE" reads as a symbol -- and this codebase's apps
        # are full of exactly that constant.
        out << ["::", PLAIN]
        i += 2
      elsif ch == ":" && ident_start?(line[i + 1, 1])
        j = i + 1
        j += 1 while j < n && ident_char?(line[j, 1])
        out << [line[i, j - i], SYMBOL]
        i = j
      elsif ch == "@"
        j = i + 1
        j += 1 while j < n && ident_char?(line[j, 1])
        out << [line[i, j - i], IVAR]
        i = j
      elsif ident_start?(ch)
        j = i
        j += 1 while j < n && ident_char?(line[j, 1])
        word = line[i, j - i]
        out << [word, word_color(word)]
        i = j
      elsif digit?(ch)
        j = number_end(line, i, n)
        out << [line[i, j - i], NUMBER]
        i = j
      else
        j = i
        j += 1 while j < n && plain_at?(line, j)
        j = i + 1 if j == i
        out << [line[i, j - i], PLAIN]
        i = j
      end
    end
    out
  end

  # Index just past the closing quote, or the end of the line for a string
  # that never closes -- an unterminated quote is a line you are still
  # typing, and colouring the rest of it as a string is what makes that
  # visible.
  def self.string_end(line, start, quote, n)
    j = start + 1
    while j < n
      c = line[j, 1]
      if c == "\\"
        j += 2
        next
      end
      return j + 1 if c == quote
      j += 1
    end
    n
  end

  # Digits, underscores and hex letters, plus a '.' only when a digit
  # follows it -- so "1.5" is one number but "1.upto" is a number and then
  # a method call.
  def self.number_end(line, start, n)
    j = start
    while j < n
      c = line[j, 1]
      if digit?(c) || c == "_" || hex_char?(c)
        j += 1
      elsif c == "." && digit?(line[j + 1, 1])
        j += 1
      else
        break
      end
    end
    j
  end

  def self.word_color(word)
    return KEYWORD if KEYWORDS.include?(word)
    return SYMBOL if (word[0, 1] =~ /[A-Z]/)
    PLAIN
  end

  def self.plain_at?(line, j)
    c = line[j, 1]
    return false if c == "#" || c == "\"" || c == "'" || c == "@"
    return false if ident_start?(c) || digit?(c)
    return false if c == ":" && line[j + 1, 1] == ":"
    return false if c == ":" && ident_start?(line[j + 1, 1])
    true
  end

  def self.ident_start?(c)
    return false if c.nil?
    (c =~ /[A-Za-z_]/) ? true : false
  end

  def self.ident_char?(c)
    return false if c.nil?
    (c =~ /[A-Za-z0-9_]/) ? true : false
  end

  def self.digit?(c)
    return false if c.nil?
    (c =~ /[0-9]/) ? true : false
  end

  def self.hex_char?(c)
    return false if c.nil?
    (c =~ /[xXa-fA-F]/) ? true : false
  end
end
