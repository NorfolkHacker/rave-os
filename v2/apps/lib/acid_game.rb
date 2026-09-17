class AcidGame < AcidApp
  TICK_MS = 50

  def on_tick
  end

  def start
    on_create
    running = true
    next_tick_at = Time.now.to_f + ( self.class::TICK_MS / 1000.0 )
    while running
      remaining_ms = ( ( next_tick_at - Time.now.to_f ) * 1000 ).to_i
      remaining_ms = 0 if remaining_ms < 0
      ev = acid_poll_event(remaining_ms)
      if ev == :close
        running = false
      elsif ev == :moved
        # No-op: unlike AcidApp, a game redraws its whole scene every
        # tick (on_tick's contract, see the design spec), so a stale
        # chrome position after a drag self-corrects on the very next
        # tick without a special case here.
      elsif ev.is_a?(Array) && ev[0] == :key
        on_key(ev[1], ev[2])
      elsif ev
        on_touch(ev[0], ev[1], ev[2])
      end
      if running && Time.now.to_f >= next_tick_at
        on_tick
        next_tick_at += ( self.class::TICK_MS / 1000.0 )
        # Resync instead of bursting through missed ticks if something
        # (a long block, a slow drain) put us more than a full tick
        # behind -- a catch-up burst would look like the game briefly
        # speeding up, exactly the class of bug this fix exists to
        # remove.
        next_tick_at = Time.now.to_f + ( self.class::TICK_MS / 1000.0 ) if next_tick_at < Time.now.to_f
      end
    end
    on_destroy
  end
end
