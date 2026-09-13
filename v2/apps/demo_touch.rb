class DemoTouchApp < AcidApp
  def on_create
    acid_fill_rect(0, 0, 320, 240, 0x000000)
  end

  def on_touch(x, y, pressed)
    acid_fill_rect(x - 5, y - 5, 10, 10, 0x00FF66) if pressed
  end
end

DemoTouchApp.new.start
