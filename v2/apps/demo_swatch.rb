class DemoSwatchApp < AcidApp
  SWATCHES = [0x00FF66, 0xFF0066, 0x0066FF, 0xFFFFFF].freeze

  def on_create
    @index = 0
    acid_fill_rect(0, 0, 140, 100, 0x050607)
  end

  def on_touch(x, y, pressed)
    return unless pressed
    @index = (@index + 1) % SWATCHES.size
    acid_fill_rect(0, 0, 140, 100, SWATCHES[@index])
  end
end

DemoSwatchApp.new.start
