# Video-game sprites for the kernel overlay (core/kernel/kernel_overlay.h).
#
# A sprite is written as a picture: an array of equal-length strings, one
# character per pixel, plus a palette mapping each character to a colour.
# "." is transparent. That means a sprite is edited by redrawing it in the
# source, not by recomputing coordinates:
#
#   TEAPOT = [ "..WWW..",
#              ".WWWWW.",
#              "WWWWWWW" ]
#   PALETTE = { "W" => 0xE8E8F0 }
#
# Draws through acid_overlay_fill_rect, so coordinates are screen-absolute
# and the result appears over every window -- see kernel_overlay.h. Nothing
# here is window-aware, and nothing here is specific to the easter eggs
# (apps/lib/acid_eggs.rb) that are its first caller.
module AcidSprite
  def self.width(rows)
    rows[0].length
  end

  def self.height(rows)
    rows.length
  end

  # `flip` mirrors horizontally, so one drawing of a character can face
  # either way -- what makes a sprite flying right-to-left look like it is
  # facing the way it is going rather than flying backwards.
  #
  # Draws runs of identical colour as single rects rather than one rect per
  # pixel. A 16x12 sprite is ~190 pixels; at 30fps that would be ~5,700
  # binding calls a second, each taking gfx's lock and crossing into LGFX.
  # Merged, the same sprite is a few dozen.
  def self.draw(rows, x, y, scale, palette, flip = false)
    r = 0
    while r < rows.length
      row = rows[r]
      draw_row(row, x, y + r * scale, scale, palette, flip)
      r += 1
    end
  end

  def self.draw_row(row, x, y, scale, palette, flip)
    len = row.length
    c = 0
    while c < len
      ch = flip ? row[len - 1 - c, 1] : row[c, 1]
      color = (ch == ".") ? nil : palette[ch]
      if color.nil?
        c += 1
        next
      end

      run = 1
      while c + run < len
        nch = flip ? row[len - 1 - (c + run), 1] : row[c + run, 1]
        break unless nch == ch
        run += 1
      end

      acid_overlay_fill_rect(x + c * scale, y, run * scale, scale, color)
      c += run
    end
  end
end
