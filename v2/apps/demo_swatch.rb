class DemoSwatchApp < AcidApp
  # RaveOS v1's own documented palette (docs/BUILD_LOG.md / kernel_theme.h)
  # -- no invented colors: THEME_HARD, THEME_PANEL, THEME_TEXT, THEME_MUTED,
  # THEME_BG, in that order.
  SWATCHES = [0x00FF66, 0x0B1712, 0xD4E6DB, 0x9DAAA3, 0x050607].freeze

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
