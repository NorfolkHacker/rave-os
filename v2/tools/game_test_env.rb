# Shared test environment for the AcidGame apps (apps/acid_blaster.rb,
# apps/breakout.rb), loaded BEFORE the app under test -- each app file
# ends in `<Class>.new.start`, so the AcidGame stub below has to exist
# (and has to NOT enter a real event loop) before that line runs. Every
# acid_* binding those games call is stubbed; the audio ones record what
# they were called with, since "was the note ever turned off" is the
# whole point of these tests.
#
# Run: see the command at the top of each test_*.rb.

$notes = []

def acid_play_note(voice, ona, volume)
  $notes << [ :play, voice, ona, volume ]
end

def acid_trigger_arp(voice, n0, n1, n2, n3, count, rate_ms)
  $notes << [ :arp, voice, n0, n1, n2, n3, count, rate_ms ]
end

def acid_stop_note(voice)
  $notes << [ :stop, voice ]
end

def acid_configure_voice(voice, wave, attack, decay, sustain, release); end
def acid_configure_filter(cutoff, resonance, mode); end

# Drawing stubs -- these tests are about audio and state, so the only
# thing recorded is that drawing happened at all.
$draw_calls = 0
def acid_clear_user_area; $draw_calls += 1; end
def acid_draw_window_frame(title); $draw_calls += 1; end
def acid_draw_window_border; $draw_calls += 1; end
def acid_draw_text(str, x, y, fg, bg); $draw_calls += 1; end
def acid_fill_circle(x, y, r, color); $draw_calls += 1; end
$rects = []
def acid_fill_rect(x, y, w, h, color)
  $draw_calls += 1
  $rects << [ x, y, w, h, color ]
end

class AcidApp
  def window_title
    "Test"
  end

  def focused?
    true
  end
end

class AcidGame < AcidApp
  TICK_MS = 50

  # The real one runs an event loop forever. Tests drive on_tick /
  # on_touch by hand instead, so this only does the on_create half and
  # hands the instance to the test file.
  def start
    on_create
    $game = self
  end
end
