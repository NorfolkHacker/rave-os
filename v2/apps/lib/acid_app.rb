class AcidApp
  def on_create
  end

  def on_touch(x, y, pressed)
  end

  def on_key(code, pressed)
  end

  def on_idle
  end

  def on_destroy
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
  end

  # Every window was previously chrome with no label at all -- just a bare
  # title-bar-colored strip and a close dot, no way to tell which app a
  # window even was without touching it. Derived from the class name
  # automatically (DemoTouchApp -> "Demo Touch") so every app gets a real
  # title with no per-app boilerplate; override this method for a custom
  # one. Capped at 16 chars -- narrow windows (140px) don't have room for
  # much more, and acid_draw_window_frame doesn't clip against the close
  # button itself, so an overlong title would run into it.
  def window_title
    name = self.class.name.sub(/App$/, "").gsub(/([a-z])([A-Z])/, '\1 \2')
    name[0, 16]
  end

  def start
    on_create
    redraw
    running = true
    while running
      ev = acid_poll_event(200)
      if ev == :close
        running = false
      elsif ev == :moved
        redraw
        acid_notify_redraw_done
      elsif ev.is_a?(Array) && ev[0] == :key
        on_key(ev[1], ev[2])
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      else
        on_idle
      end
    end
    on_destroy
  end
end
