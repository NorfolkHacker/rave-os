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
    acid_draw_window_frame
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
