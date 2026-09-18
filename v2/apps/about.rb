class AboutApp < AcidApp
  WINDOW_W = 180
  WINDOW_H = 150
  TITLE_BAR_H = 16
  LINE_H = 10
  ABOUT_FILE = "v2/fsroot/Help/about.txt"

  TEXT_COLOR = 0xD4E6DB  # THEME_TEXT
  BG_COLOR = 0x050607    # THEME_BG

  def on_create
    @lines = read_lines
  end

  # Reads Help/about.txt rather than hardcoding its own copy of the same
  # text -- one place to update, and it doubles as a live example of what
  # file_manager's Help folder is for.
  def read_lines
    f = File.open(ABOUT_FILE, "r")
    text = f.read
    f.close
    text.split("\n")
  rescue
    [ "acid OS v2" ]
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    y = TITLE_BAR_H + 4
    @lines.each do |line|
      break if y + LINE_H > WINDOW_H
      acid_draw_text(line[0, 26], 4, y, TEXT_COLOR, BG_COLOR)
      y += LINE_H
    end
    acid_draw_window_border
  end
end

AboutApp.new.start
