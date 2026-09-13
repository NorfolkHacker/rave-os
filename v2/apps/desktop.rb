class DesktopApp < AcidApp
  def on_create
    acid_draw_desktop_strip
  end

  def redraw
    acid_draw_desktop_strip
  end
end

DesktopApp.new.start
