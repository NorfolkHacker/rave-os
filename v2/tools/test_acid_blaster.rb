# Headless tests for AcidBlaster (apps/acid_blaster.rb). Loaded AFTER the
# app itself; game_test_env.rb (loaded before it) supplies the stubbed
# bindings and the non-looping AcidGame.
#
# Run (this runtime has no require, so the sources are concatenated in,
# the way vm_host loads them into a real app VM):
#
#   cd /home/norfolkh/os && cat v2/apps/lib/acid_palette.rb \
#     v2/tools/game_test_env.rb v2/apps/acid_blaster.rb \
#     v2/tools/test_acid_blaster.rb | ./v2/components/mruby/build/host/bin/mruby -

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

def ok(cond, what)
  eq(!!cond, true, what)
end

def group(name)
  puts name
end

G = $game
OVER = AcidBlaster::OVER_VOICE

# Ticks the game until it is game over, so the tests drive the real
# spawn/fly/collide path rather than poking at ivars. Returns the number
# of ticks it took.
def tick_to_game_over(limit = 400)
  n = 0
  while n < limit
    G.on_tick
    n += 1
    return n if G.instance_variable_get(:@game_over)
  end
  nil
end

def over_events
  $notes.select { |n| n[1] == OVER }
end

group "game-over sound lifecycle"

G.reset_game
$notes = []
ticks = tick_to_game_over
ok(ticks, "reaches game over by flying an enemy into the target")
eq(over_events.length > 0, true, "game over triggers the OVER voice")
eq(over_events[0][0], :play, "game over plays a note on the OVER voice")

# The gate must not outlast one full pass of its own arpeggio. The arp
# cycles with wraparound until the note is stopped (see synth.h), so a
# gate longer than OVER_ARP_COUNT steps restarts the descending run
# partway through instead of ending on its last note.
gate_ms = AcidBlaster::OVER_TICKS * AcidGame::TICK_MS
pass_ms = AcidBlaster::OVER_ARP_COUNT * AcidBlaster::OVER_ARP_RATE_MS
eq(gate_ms <= pass_ms, true,
   "game-over gate (#{gate_ms}ms) does not outlast one arp pass (#{pass_ms}ms)")

# Left alone, tick_sfx stops it on schedule.
$notes = []
AcidBlaster::OVER_TICKS.times { G.on_tick }
eq(over_events.map { |n| n[0] }, [ :stop ],
   "the OVER voice is stopped after OVER_TICKS ticks")

group "restarting mid-sound"

G.reset_game
$notes = []
tick_to_game_over
eq(over_events[0][0], :play, "game over sounded again on the second round")

# The player taps to restart while the game-over sound is still gated.
# reset_game throws away the pending sfx bookkeeping, so unless it stops
# the voice first, nothing will ever send the note-off -- the arpeggio
# keeps cycling and the envelope keeps sustaining forever.
$notes = []
G.on_touch(10, 30, true)
eq(G.instance_variable_get(:@game_over), false, "tapping restarts the game")
eq(over_events.map { |n| n[0] }, [ :stop ],
   "restarting stops the game-over voice immediately")

# Bounded well below the ~37 ticks the fastest possible loss needs (a
# top/bottom spawn is 74px from the target, and a fresh round's speed is
# 2px/tick), so a legitimate second game-over can't masquerade as a
# stray one here.
$notes = []
15.times { G.on_tick }
eq(G.instance_variable_get(:@game_over), false, "the fresh round is still running")
eq(over_events.select { |n| n[0] == :play }.length, 0,
   "no stray re-trigger of the OVER voice after the restart")

group "enemy colours"

G.reset_game
colors = []
200.times do
  G.spawn_enemy
  colors << G.instance_variable_get(:@enemies).last[:color]
end
ok(colors.all? { |c| c.is_a?(Integer) }, "every enemy spawns with a colour")
greens = colors.select { |c| c == AcidBlaster::ENEMY_COLOR }.length
ok(greens > colors.length / 2, "most enemies are still the usual green (#{greens}/200)")
ok(greens < colors.length, "but some spawn a different colour (#{200 - greens}/200)")
ok((colors - [ AcidBlaster::ENEMY_COLOR ]).uniq.length > 1,
   "the off-colour enemies are not all one single alternate shade")

# The colour has to reach the screen, not just the hash.
G.reset_game
G.instance_variable_set(:@enemies, [ { x: 100, y: 100, dx: 0, dy: 0, color: 0xFF00FF } ])
$rects = []
G.draw
ok($rects.any? { |r| r[4] == 0xFF00FF }, "an enemy is drawn in its own colour")

group "background stars"

G.reset_game
stars = G.instance_variable_get(:@stars)
eq(stars.length, AcidBlaster::STAR_COUNT, "a full sky is generated")
ok(stars.all? { |s| s[:y] >= AcidBlaster::STAR_TOP },
   "no star sits in the SCORE line's text band")
ok(stars.all? { |s| s[:y] < AcidBlaster::TITLE_BAR_H + AcidBlaster::PLAY_H },
   "no star sits below the play field")
ok(stars.all? { |s|
     s[:x] >= AcidBlaster::STAR_MARGIN &&
       s[:x] <= AcidBlaster::WINDOW_W - 1 - AcidBlaster::STAR_MARGIN &&
       s[:y] <= AcidBlaster::WINDOW_H - 1 - AcidBlaster::STAR_MARGIN
   }, "no star sits on the window border or a rounded corner")
ok(stars.all? { |s| AcidBlaster::STAR_COLORS.include?(s[:color]) },
   "stars only use the dim star palette")
ok(stars.all? { |s|
     ddx = s[:x] - AcidBlaster::CENTER_X
     ddy = s[:y] - AcidBlaster::CENTER_Y
     (ddx * ddx + ddy * ddy) > (AcidBlaster::TARGET_R1 * AcidBlaster::TARGET_R1)
   }, "no star is hidden under the target")

# Stars have to be repainted every frame, or an enemy's erase patch
# swallows the ones it flew over (the same reason draw_target is).
G.reset_game
G.draw                       # first draw: full repaint
$rects = []
G.on_tick                    # a plain incremental frame
star_px = $rects.select { |r| r[2] == 1 && r[3] == 1 }
eq(star_px.length, AcidBlaster::STAR_COUNT, "every star is repainted on an incremental frame")

# ...including the frame that puts the game-over screen up, which is the
# one that erases the last aliens and then stops repainting.
G.reset_game
G.on_tick
n = 0
while n < 400
  $rects = []
  G.on_tick
  n += 1
  break if G.instance_variable_get(:@game_over)
end
star_px = $rects.select { |r| r[2] == 1 && r[3] == 1 }
eq(star_px.length, AcidBlaster::STAR_COUNT, "the stars survive the game-over screen")

puts $fails == 0 ? "ALL PASS" : "#{$fails} FAILURE(S)"
