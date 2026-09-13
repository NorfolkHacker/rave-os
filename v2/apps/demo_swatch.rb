class DemoSwatchApp < AcidApp
  SWATCHES = [0x00FF66, 0xFF0066, 0x0066FF, 0xFFFFFF].freeze

  def on_create
    @index = 0
  end

  def on_touch(x, y, pressed)
    return unless pressed
    @index = (@index + 1) % SWATCHES.size
    acid_fill_rect(0, 16, 140, 100 - 16, SWATCHES[@index])
  end
end

DemoSwatchApp.new.start
