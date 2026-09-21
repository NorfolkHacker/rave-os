# Headless tests for Breakout (apps/breakout.rb) -- specifically its SFX
# lifecycle, which is the same shape as acid_blaster's and had the same
# leak. Loaded AFTER the app; game_test_env.rb (loaded before it)
# supplies the stubbed bindings and the non-looping AcidGame.
#
# Run (this runtime has no require, so the sources are concatenated in,
# the way vm_host loads them into a real app VM):
#
#   cd /home/norfolkh/os && cat v2/apps/lib/acid_palette.rb \
#     v2/tools/game_test_env.rb v2/apps/breakout.rb \
#     v2/tools/test_breakout.rb | ./v2/components/mruby/build/host/bin/mruby -

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
OVER = Breakout::OVER_VOICE
PADDLE = Breakout::PADDLE_VOICE

def stops
  $notes.select { |n| n[0] == :stop }.map { |n| n[1] }
end

# Drops the ball out of the bottom of the field, then ticks once -- the
# real update_ball path then sets @game_over and sounds the sting, with
# no dependence on where a bouncing ball happens to be.
def lose_the_ball
  ball = G.instance_variable_get(:@ball)
  ball[:y] = Breakout::WINDOW_H + Breakout::BALL_R + 10
  G.on_tick
end

group "game-over sound lifecycle"

G.reset_game
$notes = []
lose_the_ball
ok(G.instance_variable_get(:@game_over), "losing the ball ends the game")
eq($notes.select { |n| n[0] == :play }.map { |n| n[1] }, [ OVER ],
   "game over sounds the OVER voice")

# Left alone, tick_sfx stops it on schedule.
$notes = []
8.times { G.on_tick }
eq(stops, [ OVER ], "the OVER voice is stopped on schedule")

group "restarting mid-sound"

G.reset_game
lose_the_ball
$notes = []
# The player taps to restart while the sting is still gated. reset_game
# throws away the pending sfx bookkeeping, and tick_sfx is the ONLY
# thing that ever sends a note-off -- so unless the reset stops the
# voice itself, no note-off is ever coming and the arp keeps cycling
# under a sustaining envelope forever.
G.on_touch(100, 100, true)
eq(G.instance_variable_get(:@game_over), false, "tapping restarts the game")
eq(stops, [ OVER ], "restarting stops the game-over voice immediately")

# Not just the game-over voice -- anything still gated. A paddle bounce
# moments before the loss leaves two voices in flight.
G.reset_game
G.trigger_sfx(PADDLE, Breakout::PADDLE_NOTES, 2, 20, 25, 8)
lose_the_ball
$notes = []
G.on_touch(100, 100, true)
eq(stops.sort, [ PADDLE, OVER ].sort, "restarting stops every voice still in flight")

$notes = []
10.times { G.on_tick }
eq($notes.select { |n| n[0] == :play }.length, 0,
   "no stray re-trigger after the restart")
eq(stops, [], "and nothing is stopped twice")

puts $fails == 0 ? "ALL PASS" : "#{$fails} FAILURE(S)"
