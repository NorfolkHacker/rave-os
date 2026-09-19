module AcidPalette
  # A full HSV hue wheel at full saturation/value, walked in `steps` even
  # increments (256 by default) -- not a fixed table of named colors, a
  # real continuous rainbow generator, so "256 colours" is exact rather
  # than a round number. Standard piecewise-linear HSV->RGB (integer-only,
  # no trig, no Math gem needed): the hue circle splits into 6 60-degree
  # sectors, each one channel ramping while another holds at max/min.
  #
  # Existing apps mostly drew from kernel_theme.h's five UI colors for
  # their own content too (Tetris pieces, Breakout bricks, Acid Blaster
  # enemies), which is why everything but the chrome looked like the same
  # few shades of green -- this exists so any app can pull a genuinely
  # distinct, vivid color per item instead. The five kernel_theme.h colors
  # remain what every window's own chrome (title bar, border, text) draws
  # with -- this is for CONTENT an app draws, not the window frame around
  # it.
  def self.hue(step, steps = 256)
    h = ( step % steps ) * 360 / steps
    sector = h / 60
    f = h % 60
    rise = 255 * f / 60
    fall = 255 - rise
    case sector
    when 0 then r, g, b = 255, rise, 0
    when 1 then r, g, b = fall, 255, 0
    when 2 then r, g, b = 0, 255, rise
    when 3 then r, g, b = 0, fall, 255
    when 4 then r, g, b = rise, 0, 255
    else        r, g, b = 255, 0, fall
    end
    ( r << 16 ) | ( g << 8 ) | b
  end
end
