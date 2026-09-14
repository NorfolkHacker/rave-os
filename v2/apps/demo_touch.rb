class DemoTouchApp < AcidApp
  def on_touch(x, y, pressed)
    if pressed
      acid_fill_rect(x - 5, y - 5, 10, 10, 0x00FF66)
      ona = 40 + (x / 3)
      acid_play_note(0, ona, 80)
    else
      acid_stop_note(0)
    end
  end
end

DemoTouchApp.new.start
