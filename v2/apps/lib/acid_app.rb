class AcidApp
  def on_create
  end

  def on_touch(x, y, pressed)
  end

  def on_destroy
  end

  def start
    on_create
    running = true
    while running
      ev = acid_poll_event(200)
      if ev == :close
        running = false
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      end
    end
    on_destroy
  end
end
