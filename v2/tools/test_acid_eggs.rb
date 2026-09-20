# Headless tests for AcidEggs (apps/lib/acid_eggs.rb) -- the state machine
# and geometry of the three terminal easter eggs. Every OS call it makes is
# stubbed below, and every time-dependent entry point takes an explicit
# now_ms, so the whole animation can be stepped deterministically with no OS
# and no clock underneath it.
#
# Run (this runtime has no require, so the sources are concatenated in,
# exactly the way vm_host loads them into a real app VM):
#
#   cd /home/norfolkh/os && cat v2/apps/lib/acid_sprite.rb v2/apps/lib/acid_eggs.rb \
#     v2/tools/test_acid_eggs.rb | ./v2/components/mruby/build/host/bin/mruby -

$fails = 0

def eq(actual, expected, what)
  if actual == expected
    puts "  ok  #{what}"
  else
    $fails += 1
    puts "FAIL  #{what}"
    puts "      expected #{expected.inspect}"
    puts "      got      #{actual.inspect}"
  end
end

def group(name)
  puts name
end

$rects = []
$clears = 0
$opens = 0
$closes = 0
$open_result = true
$notes = []

def acid_overlay_fill_rect(x, y, w, h, color)
  $rects << [x, y, w, h, color]
end

def acid_overlay_clear
  $clears += 1
end

def acid_overlay_open
  $opens += 1
  $open_result
end

def acid_overlay_close
  $closes += 1
end

def acid_play_note(voice, ona, volume)
  $notes << [:play, voice, ona, volume]
end

def acid_trigger_arp(voice, n0, n1, n2, n3, count, rate_ms)
  $notes << [:arp, voice, n0, n1, n2, n3, count, rate_ms]
end

def acid_stop_note(voice)
  $notes << [:stop, voice]
end

def acid_configure_voice(voice, filter_route, attack_ms, decay_ms, sustain_percent, release_ms)
  $notes << [:configure, voice]
end

def reset!
  $rects = []
  $clears = 0
  $opens = 0
  $closes = 0
  $open_result = true
  $notes = []
  AcidEggs.abort
  $closes = 0
end

group("AcidEggs: starting and stopping")

reset!
eq(AcidEggs.active?, false, "inactive before anything starts")
eq(AcidEggs.start("dave", 1000), true, "dave starts")
eq(AcidEggs.active?, true, "active once started")
eq($opens, 1, "start opens the overlay")

reset!
eq(AcidEggs.start("nope", 1000), false, "an unknown name does not start")
eq(AcidEggs.active?, false, "and leaves the eggs inactive")
eq($opens, 0, "and never opens the overlay")

reset!
$open_result = false
eq(AcidEggs.start("dave", 1000), false, "a failed overlay open does not start")
eq(AcidEggs.active?, false, "and leaves the eggs inactive")

reset!
AcidEggs.start("dave", 1000)
AcidEggs.abort
eq(AcidEggs.active?, false, "abort stops the animation")
eq($closes, 1, "abort closes the overlay")

reset!
AcidEggs.start("dave", 1000)
eq(AcidEggs.start("joe", 1000), false, "a second egg is refused while one is in flight")

group("AcidEggs: the tick guard")

reset!
AcidEggs.start("dave", 1000)
before = $clears
AcidEggs.step(1000 + AcidEggs::TICK_MS - 1)
eq($clears, before, "no frame is drawn before TICK_MS has elapsed")
AcidEggs.step(1000 + AcidEggs::TICK_MS)
eq($clears, before + 1, "a frame is drawn once TICK_MS has elapsed")

group("AcidEggs: dave and joe fly and leave")

["dave", "joe"].each do |name|
  reset!
  AcidEggs.start(name, 0)
  t = 0
  frames = 0
  while AcidEggs.active? && frames < 500
    t += AcidEggs::TICK_MS
    AcidEggs.step(t)
    frames += 1
  end
  eq(AcidEggs.active?, false, "#{name} finishes on its own")
  eq(frames < 500, true, "#{name} finishes within 500 frames (took #{frames})")
  eq($closes, 1, "#{name} closes the overlay exactly once")
  eq($rects.length > 0, true, "#{name} actually drew something")
end

group("AcidEggs: dave and joe stay on screen vertically")

# Run each egg many times: the height and direction are random per run, and
# a sprite half off the top or bottom of the screen is the bug this catches.
i = 0
while i < 40
  reset!
  AcidEggs.start("dave", 0)
  t = 0
  while AcidEggs.active? && t < 20000
    t += AcidEggs::TICK_MS
    AcidEggs.step(t)
  end
  ys = $rects.map { |r| r[1] }
  eq(ys.min >= 0, true, "dave never draws above the top of the screen")
  eq(ys.max < AcidEggs::SCREEN_H, true, "dave never draws below the bottom")
  i += 1
end

group("AcidEggs: maximbady bounces then shouts")

reset!
AcidEggs.start("maximbady", 0)
t = 0
saw_bounce_sound = false
while AcidEggs.active? && t < 60000
  t += AcidEggs::TICK_MS
  AcidEggs.step(t)
  saw_bounce_sound = true if $notes.length > 0
end
eq(AcidEggs.active?, false, "maximbady finishes on its own")
eq($closes, 1, "maximbady closes the overlay exactly once")
eq(saw_bounce_sound, true, "maximbady makes a sound")

# Centring is asserted against the layout box, not against the drawn pixels:
# both glyphs have blank columns in some of their rows, so the ink's own
# bounding box is narrower than the space the word occupies and is the wrong
# thing to measure.
box = AcidEggs.word_box
eq(box[0], (AcidEggs::SCREEN_W - box[2]) / 2, "the word box is horizontally centred")
eq(box[1], (AcidEggs::SCREEN_H - box[3]) / 2, "the word box is vertically centred")
eq(box[0] >= 0 && box[0] + box[2] <= AcidEggs::SCREEN_W, true,
   "the word fits across the screen")
eq(box[1] >= 0 && box[1] + box[3] <= AcidEggs::SCREEN_H, true,
   "the word fits down the screen")

# ...and the ink actually lands inside that box.
reset!
AcidEggs.start("maximbady", 0)
t = 0
word_rects = []
while AcidEggs.active? && t < 60000
  t += AcidEggs::TICK_MS
  before = $rects.length
  AcidEggs.step(t)
  frame = $rects[before, $rects.length - before]
  # The word frame is the one with far more rects than a single small figure.
  word_rects = frame if frame.length > word_rects.length
end
eq(word_rects.length > 0, true, "the word phase drew something")
eq(word_rects.map { |r| r[0] }.min >= box[0], true, "no glyph ink starts left of the box")
eq(word_rects.map { |r| r[0] + r[2] }.max <= box[0] + box[2], true,
   "no glyph ink runs past the right of the box")
eq(word_rects.map { |r| r[1] }.min >= box[1], true, "no glyph ink starts above the box")
eq(word_rects.map { |r| r[1] + r[3] }.max <= box[1] + box[3], true,
   "no glyph ink runs below the box")

puts ""
puts $fails == 0 ? "all passed" : "#{$fails} FAILED"
