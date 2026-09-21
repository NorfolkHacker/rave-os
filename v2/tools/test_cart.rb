# Headless tests for the cart loader's pure module (apps/cart/cartfile.rb).
# Nothing here calls an acid_* binding or touches the filesystem, so it all
# runs under the vendored host mruby with no OS underneath it -- same split
# and same harness as test_editor.rb.
#
# Run (this runtime has no require, so the sources are concatenated in,
# exactly the way vm_host loads them into a real app VM):
#
#   cd /home/norfolkh/os && cat v2/apps/cart/cartfile.rb \
#     v2/tools/test_cart.rb | ./v2/components/mruby/build/host/bin/mruby -

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

LIBS = [ "lib/acid_app.rb", "lib/acid_game.rb", "lib/acid_keys.rb" ]

# ------------------------------------------------------------------ slug
#
# The destination filename a cart installs as. This is the security-
# critical one: whatever the host file is called, the result has to be a
# bare name that can only ever land inside v2/apps.

group("slug: ordinary names")
eq(Cartfile.slug("hello_acid.cart"), "hello_acid", "plain name keeps its stem")
eq(Cartfile.slug("Acid Snake.cart"), "acid_snake", "spaces and case fold to lowercase underscores")
eq(Cartfile.slug("/media/usb/games/Blaster.cart"), "blaster", "a full path keeps only the basename")

group("slug: hostile names")
eq(Cartfile.slug("../../desktop.cart"), "desktop", "traversal components are stripped, not obeyed")
eq(Cartfile.slug("/etc/passwd.cart"), "passwd", "an absolute path cannot escape the apps dir")
eq(Cartfile.slug("a/../../../b.cart"), "b", "traversal in the middle is stripped too")
eq(Cartfile.slug("evil\nname.cart"), "evil_name", "a newline in the name cannot inject a second path")
eq(Cartfile.slug("weird!!!name.cart"), "weird_name", "a run of junk collapses to one underscore")
eq(Cartfile.slug("..cart"), nil, "a name with no usable characters is refused")
eq(Cartfile.slug("....cart"), nil, "dots alone are refused")
eq(Cartfile.slug(""), nil, "an empty name is refused")
eq(Cartfile.slug("/"), nil, "a bare slash is refused")
eq(Cartfile.slug("___.cart"), nil, "underscores alone are refused")
eq(Cartfile.slug(("z" * 60) + ".cart"), "z" * Cartfile::MAX_SLUG, "an overlong name is truncated")

group("slug: extension handling")
eq(Cartfile.slug("game.CART"), "game", "the .cart suffix matches case-insensitively")
eq(Cartfile.slug("notes.txt"), "notes_txt", "a non-cart extension is folded into the name, not stripped")

# ---------------------------------------------------------------- cart?

group("cart?: which host files are even offered")
eq(Cartfile.cart?("snake.cart"), true, "a .cart file is offered")
eq(Cartfile.cart?("SNAKE.CART"), true, "case does not matter")
eq(Cartfile.cart?("snake.rb"), false, "a stray .rb is not")
eq(Cartfile.cart?(".cart"), false, "a bare extension with no stem is not")
eq(Cartfile.cart?("cart"), false, "a file merely named cart is not")

# --------------------------------------------------------- parse_header

HEADER = <<'CART'
# name: Acid Snake
# w: 160
# h: 140
# desc: Snake, but it melts
# libs: lib/acid_game.rb, lib/acid_keys.rb

class SnakeApp < AcidApp
end
CART

group("parse_header: the documented form")
eq(Cartfile.parse_header(HEADER)["name"], "Acid Snake", "name is read")
eq(Cartfile.parse_header(HEADER)["w"], "160", "w is read as its raw string")
eq(Cartfile.parse_header(HEADER)["desc"], "Snake, but it melts", "desc is read")
eq(Cartfile.parse_header(HEADER)["libs"], "lib/acid_game.rb, lib/acid_keys.rb", "libs is read raw")

group("parse_header: tolerated spellings")
eq(Cartfile.parse_header("#name:Tight\n")["name"], "Tight", "no spaces around the colon still parses")
eq(Cartfile.parse_header("# NAME: Shouty\n")["name"], "Shouty", "keys are case-insensitive")
eq(Cartfile.parse_header("# name: CRLF\r\n# w: 90\r\n")["name"], "CRLF", "a CRLF file leaves no stray carriage return")
eq(Cartfile.parse_header("# name: CRLF\r\n# w: 90\r\n")["w"], "90", "CRLF on a numeric field too")
eq(Cartfile.parse_header("\n\n# name: Late\n")["name"], "Late", "blank lines before the header are skipped")

group("parse_header: where the header stops")
eq(Cartfile.parse_header("class Foo\n# name: Sneaky\n")["name"], nil,
   "a header line after real code is ignored")
eq(Cartfile.parse_header("# name: First\nclass Foo\n# name: Second\n")["name"], "First",
   "only the leading comment block counts")
eq(Cartfile.parse_header("class Foo\nend\n"), {}, "a cart with no header parses to nothing")
eq(Cartfile.parse_header("# just a comment\n# name: Real\n")["name"], "Real",
   "an unrelated comment does not end the header")
eq(Cartfile.parse_header("# name: X\n# evil: rm -rf\n")["evil"], nil, "unknown keys are dropped")

group("parse_header: value hygiene")
eq(Cartfile.parse_header("# name: " + ("n" * 90) + "\n")["name"].length, Cartfile::MAX_TEXT,
   "an overlong value is truncated")
eq(Cartfile.parse_header("# desc: tab\there\n")["desc"], "tab here", "control characters become spaces")
eq(Cartfile.parse_header("# name:   \n")["name"], nil, "an empty value is dropped, not stored blank")

# ----------------------------------------------------------- dimension

group("dimension: clamping a cart's window request")
eq(Cartfile.dimension("160", 220, Cartfile::MIN_W, Cartfile::MAX_W), 160, "an in-range width is kept")
eq(Cartfile.dimension(nil, 220, Cartfile::MIN_W, Cartfile::MAX_W), 220, "a missing width falls back")
eq(Cartfile.dimension("", 220, Cartfile::MIN_W, Cartfile::MAX_W), 220, "an empty width falls back")
eq(Cartfile.dimension("99999", 220, Cartfile::MIN_W, Cartfile::MAX_W), Cartfile::MAX_W,
   "an absurd width is clamped to the screen")
eq(Cartfile.dimension("-40", 220, Cartfile::MIN_W, Cartfile::MAX_W), Cartfile::MIN_W,
   "a negative width is clamped up")
eq(Cartfile.dimension("abc", 220, Cartfile::MIN_W, Cartfile::MAX_W), Cartfile::MIN_W,
   "a non-numeric width lands on the minimum, never zero")
eq(Cartfile.dimension("140", 160, Cartfile::MIN_H, Cartfile::MAX_H), 140, "heights clamp on their own bounds")

# ----------------------------------------------------------- filter_libs

group("filter_libs: only modules that actually exist")
eq(Cartfile.filter_libs("lib/acid_game.rb, lib/acid_keys.rb", LIBS),
   [ "lib/acid_game.rb", "lib/acid_keys.rb" ], "known modules pass through")
eq(Cartfile.filter_libs("lib/acid_game.rb", LIBS), [ "lib/acid_game.rb" ], "a single module passes")
eq(Cartfile.filter_libs(nil, LIBS), [], "no libs line means no modules")
eq(Cartfile.filter_libs("", LIBS), [], "an empty libs line means no modules")

group("filter_libs: hostile entries")
eq(Cartfile.filter_libs("../../../etc/passwd.rb", LIBS), [], "traversal is not a known module")
eq(Cartfile.filter_libs("/etc/shadow", LIBS), [], "an absolute path is not a known module")
eq(Cartfile.filter_libs("lib/../desktop.rb", LIBS), [], "traversal dressed as a lib path is dropped")
eq(Cartfile.filter_libs("desktop.rb", LIBS), [], "a real app outside lib/ cannot be pulled in")
eq(Cartfile.filter_libs("lib/acid_game.rb, lib/nope.rb", LIBS), [ "lib/acid_game.rb" ],
   "one bad entry does not poison the good ones")
eq(Cartfile.filter_libs("lib/acid_game.rb, lib/acid_game.rb", LIBS), [ "lib/acid_game.rb" ],
   "duplicates collapse")
eq(Cartfile.filter_libs(([ "lib/acid_game.rb" ] * 20).join(","), LIBS).length, 1,
   "a flood of duplicates still collapses to one")

# --------------------------------------------------------- manifest_text

group("manifest_text: the .app.toml a cart installs as")
MANI = Cartfile.manifest_text({ name: "Acid Snake", w: 160, h: 140,
                                desc: "Snake, but it melts", libs: [ "lib/acid_game.rb" ] })
eq(MANI.include?("name = Acid Snake\n"), true, "carries the name")
eq(MANI.include?("w = 160\n"), true, "carries the width")
eq(MANI.include?("h = 140\n"), true, "carries the height")
eq(MANI.include?("desc = Snake, but it melts\n"), true, "carries the description")
eq(MANI.include?("libs = lib/acid_game.rb\n"), true, "carries the module list")
eq(MANI.include?("source = cart\n"), true, "marks itself as cart-installed")
eq(MANI[MANI.length - 1, 1], "\n", "ends with a newline like every other manifest")

group("manifest_text: omissions")
BARE = Cartfile.manifest_text({ name: "Bare", w: 100, h: 100, desc: "", libs: [] })
eq(BARE.include?("libs"), false, "no modules means no libs line at all")
eq(BARE.include?("desc"), false, "no description means no desc line")

# -------------------------------------------------------- replaceable?

group("replaceable?: what a cart may overwrite")
eq(Cartfile.replaceable?({ "name" => "Acid Snake", "source" => "cart" }), true,
   "a previously installed cart may be replaced")
eq(Cartfile.replaceable?({ "name" => "Desktop" }), false,
   "a built-in app with no source marker is protected")
eq(Cartfile.replaceable?({ "name" => "X", "source" => "builtin" }), false,
   "any other source value is protected")
eq(Cartfile.replaceable?({}), false, "an unreadable/empty manifest is protected")
eq(Cartfile.replaceable?(nil), true, "nothing there at all is a fresh install")

# ------------------------------------------------------------ name_clash
#
# Slot protection is about files; this is about NAMES. Terminal's `run`
# resolves an app by the first case-insensitive name match in registry
# order, and a cart picks its own filename (hence its sort position), so
# a cart calling itself "Editor" from an unused slot would answer to
# `run editor` before the real Editor does. Nothing here blocks that --
# the confirm screen just has to be able to say so.

REGISTRY = [ [ "v2/apps/editor.rb", "Editor" ], [ "v2/apps/tetris.rb", "Tetris" ] ]

group("name_clash: spotting a name already in use")
eq(Cartfile.name_clash("Editor", "v2/apps/aaa.rb", REGISTRY), "v2/apps/editor.rb",
   "a cart taking a built-in's name reports the app it collides with")
eq(Cartfile.name_clash("editor", "v2/apps/aaa.rb", REGISTRY), "v2/apps/editor.rb",
   "the comparison ignores case, exactly as Terminal's lookup does")
eq(Cartfile.name_clash("Acid Snake", "v2/apps/acid_snake.rb", REGISTRY), nil,
   "an unused name is free")
eq(Cartfile.name_clash("Editor", "v2/apps/editor.rb", REGISTRY), nil,
   "an app does not collide with itself -- reinstalling a cart keeps its own name")
eq(Cartfile.name_clash("Editor", "v2/apps/aaa.rb", []), nil,
   "an empty registry collides with nothing")

# --------------------------------------------------- destination_status
#
# What the confirm screen is allowed to offer for a slug, given what is
# already sitting in that slot in v2/apps.

group("destination_status: an empty slot")
eq(Cartfile.destination_status(false, nil), :fresh, "nothing there at all installs cleanly")

group("destination_status: a slot this loader filled before")
eq(Cartfile.destination_status(true, { "name" => "Acid Snake", "source" => "cart" }), :replace,
   "a previously installed cart offers a replace")

group("destination_status: slots that must never be written")
eq(Cartfile.destination_status(true, { "name" => "Tetris" }), :protected,
   "a built-in app with a manifest is protected")
eq(Cartfile.destination_status(true, nil), :protected,
   "a bare .rb with no manifest at all -- desktop.rb -- is protected, not mistaken for empty")
eq(Cartfile.destination_status(false, { "name" => "Ghost" }), :protected,
   "a manifest with no .rb beside it is still someone else's slot")
eq(Cartfile.destination_status(true, { "name" => "X", "source" => "builtin" }), :protected,
   "any other source marker is protected")

# ----------------------------------------------------------- size_ok?

group("size_ok?: refusing outsized carts")
eq(Cartfile.size_ok?(1024), true, "an ordinary cart fits")
eq(Cartfile.size_ok?(0), false, "an empty file is refused")
eq(Cartfile.size_ok?(Cartfile::MAX_BYTES), true, "exactly the cap fits")
eq(Cartfile.size_ok?(Cartfile::MAX_BYTES + 1), false, "one byte over is refused")

# --------------------------------------------------- browse containment

group("parent_dir: .. stops dead at the root it started in")
eq(Cartfile.parent_dir("/media/usb/games", "/media"), "/media/usb", "one level up inside the root")
eq(Cartfile.parent_dir("/media/usb", "/media"), "/media", "up to the root itself")
eq(Cartfile.parent_dir("/media", "/media"), nil, "the root has no parent to offer")
eq(Cartfile.parent_dir("v2/carts", "v2/carts"), nil, "a relative root behaves the same")
eq(Cartfile.parent_dir("v2/carts/demo", "v2/carts"), "v2/carts", "a relative root walks back to itself")

group("child_dir: descending")
eq(Cartfile.child_dir("/media", "usb"), "/media/usb", "joins one path component")
eq(Cartfile.child_dir("/media/", "usb"), "/media/usb", "a trailing slash does not double up")
eq(Cartfile.child_dir("/media", ".."), nil, "'..' is never a descendable name")
eq(Cartfile.child_dir("/media", "."), nil, "'.' is never a descendable name")
eq(Cartfile.child_dir("/media", "a/b"), nil, "a name with a separator is refused")
eq(Cartfile.child_dir("/media", ""), nil, "an empty name is refused")

# ------------------------------------------------------------ from_cart
#
# The whole validated install spec the UI acts on -- everything above,
# composed.

group("from_cart: a well-formed cart")
SPEC = Cartfile.from_cart("Acid Snake.cart", HEADER, LIBS)
eq(SPEC[:slug], "acid_snake", "slug comes from the filename, not the header")
eq(SPEC[:name], "Acid Snake", "name comes from the header")
eq(SPEC[:w], 160, "width is parsed")
eq(SPEC[:h], 140, "height is parsed")
eq(SPEC[:libs], [ "lib/acid_game.rb", "lib/acid_keys.rb" ], "modules are filtered")
eq(SPEC[:rb_path], "v2/apps/acid_snake.rb", "destination source path is inside the apps dir")
eq(SPEC[:toml_path], "v2/apps/acid_snake.app.toml", "destination manifest sits beside it")

group("from_cart: a cart with no header at all")
BARESPEC = Cartfile.from_cart("mystery.cart", "class X\nend\n", LIBS)
eq(BARESPEC[:name], "Mystery", "the name falls back to the filename, title-cased")
eq(BARESPEC[:w], Cartfile::DEFAULT_W, "the width falls back to the default")
eq(BARESPEC[:h], Cartfile::DEFAULT_H, "the height falls back to the default")
eq(BARESPEC[:libs], [], "no modules are loaded by default")

group("from_cart: names that cannot install")
eq(Cartfile.from_cart("..cart", "class X\nend\n", LIBS), nil, "an unusable filename yields no spec")

group("from_cart: the header cannot reach outside the apps dir")
EVIL = Cartfile.from_cart("../../../../etc/cron.cart",
                          "# name: ../../evil\n# libs: ../../../etc/passwd.rb\n", LIBS)
eq(EVIL[:rb_path], "v2/apps/cron.rb", "a traversal filename still lands in the apps dir")
eq(EVIL[:libs], [], "a traversal libs entry is dropped")
eq(EVIL[:name], "../../evil", "the name is only ever display text, so it is kept verbatim")

puts ""
puts $fails == 0 ? "ALL PASS" : "#{$fails} FAILURE(S)"
