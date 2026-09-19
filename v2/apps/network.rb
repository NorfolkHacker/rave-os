# Network -- this device's real hostname and LAN address, from
# hal_network_get_info. There is nothing to configure here: acid OS v2 has
# no network stack of its own yet (no sockets, no WiFi driver on hw), so
# this is a status readout, not a settings panel -- see Config for the one
# app in this OS that actually changes something. Re-reads on a small
# timer since the underlying address can change (DHCP renewal, cable
# unplugged) without this app itself doing anything.
class NetworkApp < AcidApp
  WINDOW_W = 200
  WINDOW_H = 110
  TITLE_BAR_H = 16
  LINE_H = 12
  REFRESH_SECS = 3.0

  BG_COLOR = 0x050607     # THEME_BG
  TEXT_COLOR = 0xD4E6DB   # THEME_TEXT
  MUTED_COLOR = 0x9DAAA3  # THEME_MUTED
  HARD_COLOR = 0x00FF66   # THEME_HARD

  def on_create
    @host = "?"
    @ip = "?"
    @connected = false
    @next_refresh_at = 0
    refresh
  end

  def window_title
    "Network"
  end

  def on_idle
    refresh
  end

  def refresh
    now = Time.now.to_f
    return if now < @next_refresh_at
    @next_refresh_at = now + REFRESH_SECS
    host, ip, connected = acid_network_info
    changed = host != @host || ip != @ip || connected != @connected
    @host = host
    @ip = ip
    @connected = connected
    redraw if changed
  end

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)

    y = TITLE_BAR_H + 6
    dot_color = @connected ? HARD_COLOR : MUTED_COLOR
    acid_fill_rect(4, y + 2, 6, 6, dot_color)
    acid_draw_text(@connected ? "connected" : "no address found", 14, y, @connected ? TEXT_COLOR : MUTED_COLOR, BG_COLOR)
    y += LINE_H + 4

    acid_draw_text("HOST", 4, y, MUTED_COLOR, BG_COLOR)
    y += LINE_H
    acid_draw_text(@host[0, 30], 4, y, TEXT_COLOR, BG_COLOR)
    y += LINE_H + 4

    acid_draw_text("IP", 4, y, MUTED_COLOR, BG_COLOR)
    y += LINE_H
    acid_draw_text(@ip[0, 30], 4, y, TEXT_COLOR, BG_COLOR)

    acid_draw_window_border
  end
end

NetworkApp.new.start
