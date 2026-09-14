class AcidGame < AcidApp
  TICK_MS = 50

  def on_tick
  end

  def start
    on_create
    running = true
    while running
      ev = acid_poll_event(self.class::TICK_MS)
      if ev == :close
        running = false
      elsif ev == :moved
        # No-op: unlike AcidApp, a game redraws its whole scene every
        # tick (on_tick's contract, see the design spec), so a stale
        # chrome position after a drag self-corrects on the very next
        # tick without a special case here.
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      end
      on_tick if running
    end
    on_destroy
  end
end
