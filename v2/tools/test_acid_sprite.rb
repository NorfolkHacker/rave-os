# Headless tests for AcidSprite (apps/lib/acid_sprite.rb). The module's only
# OS dependency is acid_overlay_fill_rect, stubbed below to record calls, so
# this runs under the vendored host mruby with no OS underneath it.
#
# Run (this runtime has no require, so the sources are concatenated in,
# exactly the way vm_host loads them into a real app VM):
#
#   cd /home/norfolkh/os && cat v2/apps/lib/acid_sprite.rb \
#     v2/tools/test_acid_sprite.rb | ./v2/components/mruby/build/host/bin/mruby -

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

def acid_overlay_fill_rect(x, y, w, h, color)
  $rects << [x, y, w, h, color]
end

PAL = { "R" => 0xFF0000, "B" => 0x0000FF }

group("AcidSprite: geometry")

eq(AcidSprite.width(["..RR", "RRRR"]), 4, "width is the row length")
eq(AcidSprite.height(["..RR", "RRRR"]), 2, "height is the row count")

group("AcidSprite: drawing")

$rects = []
AcidSprite.draw(["R"], 10, 20, 1, PAL)
eq($rects, [[10, 20, 1, 1, 0xFF0000]], "a single pixel is one 1x1 rect at the origin")

$rects = []
AcidSprite.draw(["R"], 10, 20, 3, PAL)
eq($rects, [[10, 20, 3, 3, 0xFF0000]], "scale 3 makes each pixel a 3x3 block")

$rects = []
AcidSprite.draw([".R"], 10, 20, 3, PAL)
eq($rects, [[13, 20, 3, 3, 0xFF0000]], "'.' is transparent and shifts the next pixel")

$rects = []
AcidSprite.draw(["R", "B"], 10, 20, 2, PAL)
eq($rects, [[10, 20, 2, 2, 0xFF0000], [10, 22, 2, 2, 0x0000FF]],
   "rows advance by scale in y")

$rects = []
AcidSprite.draw(["..."], 10, 20, 2, PAL)
eq($rects, [], "an all-transparent row draws nothing")

$rects = []
AcidSprite.draw(["X"], 10, 20, 2, PAL)
eq($rects, [], "a character missing from the palette draws nothing")

group("AcidSprite: horizontal run merging")

# One fill per pixel would be ~190 binding calls per frame at 30fps for a
# 16x12 sprite, each taking the gfx lock and crossing into LGFX. Merging
# runs of the same colour cuts that by an order of magnitude and is why
# draw walks runs rather than pixels.
$rects = []
AcidSprite.draw(["RRR"], 0, 0, 2, PAL)
eq($rects, [[0, 0, 6, 2, 0xFF0000]], "three same-colour pixels become one wide rect")

$rects = []
AcidSprite.draw(["RRBB"], 0, 0, 1, PAL)
eq($rects, [[0, 0, 2, 1, 0xFF0000], [2, 0, 2, 1, 0x0000FF]],
   "a colour change ends the run")

$rects = []
AcidSprite.draw(["RR.R"], 0, 0, 1, PAL)
eq($rects, [[0, 0, 2, 1, 0xFF0000], [3, 0, 1, 1, 0xFF0000]],
   "a transparent gap ends the run")

group("AcidSprite: flip")

$rects = []
AcidSprite.draw([".R"], 0, 0, 1, PAL, true)
eq($rects, [[0, 0, 1, 1, 0xFF0000]], "flip mirrors the row horizontally")

$rects = []
AcidSprite.draw(["RRB"], 0, 0, 1, PAL, true)
eq($rects, [[0, 0, 1, 1, 0x0000FF], [1, 0, 2, 1, 0xFF0000]],
   "flip preserves run merging in mirrored order")

puts ""
puts $fails == 0 ? "all passed" : "#{$fails} FAILED"
