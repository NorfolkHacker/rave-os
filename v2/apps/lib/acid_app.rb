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
    acid_draw_window_border
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

  # True if this app's own window currently holds keyboard focus, which in
  # this codebase always means "is the actual topmost/visible window" too
  # (kernel_router_activate_window sets both together, nothing changes
  # either one independently). AcidGame uses this to avoid redrawing
  # itself while covered by another window (see its own comment); any
  # app is free to use it too.
  def focused?
    acid_am_i_focused
  end

  # Ends the run loop from inside the app itself -- e.g. the editor's
  # ESC q close confirmation. The other two ways this loop ends are the
  # title-bar close button and the kernel's own :close event, both of
  # which arrive here as that same event, not through this method.
  def quit!
    @running = false
  end

  # v2/fsroot/App is a real symlink to v2/apps -- an OS layout fact, not
  # something specific to any one app, which is why this lives here on
  # the shared base class every app's VM already loads, rather than
  # being duplicated into each app that happens to need it. (It was
  # duplicated into EditorCmd and FileManagerApp at first, on the
  # mistaken belief they had no shared ancestry to hang it on -- they do,
  # this one -- and two copies of a symlink mapping is exactly the kind
  # of thing that drifts if the layout ever changes.)
  #
  # window_binding.c's is_multi_by_path/libs_by_path match a launch path
  # against the launcher registry by exact strcmp against its canonical
  # v2/apps form, so a path that arrived through the symlink matches
  # neither and the spawned VM silently loads none of its libs. Fixed in
  # Ruby, not with a C-side realpath: the hw target's filesystem layer is
  # a stub, and a canonicalisation that only works on the sim would be
  # worse than this explicit, commented mapping of the one symlink that
  # exists.
  FSROOT_APP_PREFIX = "v2/fsroot/App/"
  CANONICAL_APP_PREFIX = "v2/apps/"

  def canonical_app_path(path)
    return path unless path.start_with?(FSROOT_APP_PREFIX)
    CANONICAL_APP_PREFIX + path[FSROOT_APP_PREFIX.length, path.length - FSROOT_APP_PREFIX.length]
  end

  # How long acid_poll_event blocks when there is no event waiting, and so
  # how often on_idle fires. 200ms is right for an app that only redraws in
  # response to input; an app animating something (the terminal, while an
  # easter egg is in flight -- see apps/lib/acid_eggs.rb) overrides this to
  # a frame interval while the animation runs and returns to 200 after. Not
  # a constant, because the answer changes while the app is running.
  def poll_timeout_ms
    200
  end

  def start
    on_create
    redraw
    @running = true
    while @running
      # Clamp rather than trust poll_timeout_ms outright: it reaches
      # acid_poll_event -> pdMS_TO_TICKS as-is, and a subclass override of
      # 0 would busy-spin the event loop at 100% CPU, while a negative one
      # converts (TickType_t is unsigned) into a huge tick count -- in
      # practice an effectively infinite block, so on_idle would never fire
      # again for that app. 1ms is the smallest wait that still blocks.
      # The normal path (a positive override, or the 200 default) is
      # unaffected -- max(1, 200) is still 200.
      ev = acid_poll_event([poll_timeout_ms, 1].max)
      if ev == :close
        @running = false
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
