# Tests for the half of the cart loader that actually touches files --
# browsing a card, reading a cart, and writing it into the apps dir. The
# pure decisions live in apps/cart/cartfile.rb and are covered by
# test_cart.rb; this covers what CartApp does with them, against a real
# (throwaway) filesystem, since "refuses to overwrite desktop.rb" is a
# claim about files, not about strings.
#
# Run (cart_test_env.rb stubs the bindings and builds the fixture tree,
# so it has to come first; cart.rb instantiates itself at the bottom,
# which is why the stubbed AcidApp#start does nothing):
#
#   cd /home/norfolkh/os && cat v2/tools/cart_test_env.rb \
#     v2/apps/cart/cartfile.rb v2/apps/cart.rb \
#     v2/tools/test_cart_install.rb | ./v2/components/mruby/build/host/bin/mruby -

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

def read(path)
  f = File.open(path, "r")
  t = f.read
  f.close
  t
end

def exists?(path)
  f = File.open(path, "r")
  f.close
  true
rescue
  false
end

def app
  @app ||= CartApp.new
end

def ivar(name)
  app.instance_variable_get(name)
end

# Opens the repo cart root and selects the entry with this filename,
# the way tapping its row does.
def select_cart(name)
  app.open_root("v2/carts")
  entries = ivar(:@entries)
  i = 0
  while i < entries.length
    break if entries[i][:name] == name
    i += 1
  end
  raise "no such entry: #{name}" if i >= entries.length
  app.instance_variable_set(:@selected, i)
  app.open_selected
end

app.on_create

# ---------------------------------------------------------------- roots

group("roots: which cards are offered")
eq(ivar(:@roots).include?("v2/carts"), true, "the repo's own carts directory is a root")
eq(ivar(:@roots).include?("/etc"), false, "nothing outside the configured roots is offered")
eq(ivar(:@screen), :roots, "the app opens on the card list")

# -------------------------------------------------------------- listing

app.open_root("v2/carts")

group("listing: what a card shows")
names = ivar(:@entries).map { |e| e[:name] }
eq(names.include?("hello.cart"), true, "a .cart file is listed")
eq(names.include?("games"), true, "a subdirectory is listed")
eq(names.include?("notes.txt"), false, "an unrelated file on the card is not listed")
eq(names.include?(".."), false, "the root itself offers no way up out of it")
eq(names.include?("escape"), false, "a symlink is skipped rather than followed out of the root")

group("listing: descending and coming back")
app.instance_variable_set(:@selected, names.index("games"))
app.open_selected
eq(ivar(:@dir), "v2/carts/games", "opening a directory descends into it")
eq(ivar(:@entries).map { |e| e[:name] }.include?(".."), true, "a subdirectory offers a way back up")
app.go_up
eq(ivar(:@dir), "v2/carts", "going up returns to the root")
app.go_up
eq(ivar(:@screen), :roots, "going up from the root returns to the card list")

# ------------------------------------------------------- a clean install

select_cart("hello.cart")

group("confirm: what the cart asked for")
eq(ivar(:@screen), :confirm, "selecting a cart opens the confirm screen")
eq(ivar(:@status), :fresh, "an unused slot is a fresh install")
spec = ivar(:@spec)
eq(spec[:name], "Hello Acid", "the header's name is used")
eq(spec[:w], 200, "the header's width is used")
eq(spec[:rb_path], "v2/apps/hello.rb", "the destination is inside the apps dir")
eq(spec[:libs], [ "lib/acid_palette.rb" ],
   "a real module is kept and the traversal entry beside it is dropped")

app.install

group("install: what landed on disk")
eq(ivar(:@screen), :done, "a successful install moves on")
eq(exists?("v2/apps/hello.rb"), true, "the source was written")
eq(read("v2/apps/hello.rb"), HELLO_CART, "the source is a byte-for-byte copy of the cart")
eq(read("v2/apps/hello.app.toml"),
   "name = Hello Acid\nw = 200\nh = 150\ndesc = Example cart\nlibs = lib/acid_palette.rb\nsource = cart\n",
   "the generated manifest carries the header and marks itself cart-installed")

group("install: running it straight away")
app.launch
eq($registered.length, 1, "the new app is registered once")
eq($registered[0], [ "v2/apps/hello.rb", "Hello Acid", 200, 150, false, "lib/acid_palette.rb" ],
   "it registers with the manifest's own values")
eq($spawned, [ [ "v2/apps/hello.rb", 200, 150, "" ] ], "and is spawned")
app.launch
eq($registered.length, 1, "launching twice does not register it twice")

# ------------------------------------------------ a name already in use

select_cart("impostor.cart")

group("name clash: a cart claiming an installed app's name")
eq(ivar(:@status), :fresh, "its own slot is free, so the install itself is allowed")
eq(ivar(:@name_clash), "v2/apps/hello.rb",
   "but the confirm screen knows whose name it is taking")

select_cart("hello.cart")
eq(ivar(:@name_clash), nil, "reinstalling a cart does not collide with itself")

# ------------------------------------------------------- replacing a cart

select_cart("hello.cart")

group("replace: a slot this loader filled before")
eq(ivar(:@status), :replace, "an installed cart offers a replace, not a fresh install")
app.install
eq(ivar(:@screen), :done, "the replace goes through")

# ------------------------------------------------- refusing built-in apps

select_cart("desktop.cart")

group("protected: a built-in app with no manifest of its own")
eq(ivar(:@status), :protected, "desktop.rb's slot is protected")
app.install
eq(read("v2/apps/desktop.rb"), "# the real desktop\n", "the real desktop is untouched")
eq(exists?("v2/apps/desktop.app.toml"), false, "and no manifest was invented for it")
eq(ivar(:@screen), :confirm, "the app stays on the confirm screen rather than reporting success")

select_cart("tetris.cart")

group("protected: a built-in app with a manifest")
eq(ivar(:@status), :protected, "a manifest with no cart marker is protected")
app.install
eq(read("v2/apps/tetris.rb"), "# the real tetris\n", "the real tetris is untouched")
eq(read("v2/apps/tetris.app.toml"), "name = Tetris\nw = 160\nh = 160\n", "its manifest is untouched")

# ---------------------------------------------------------- oversized

select_cart("big.cart")

group("oversized: refused before it is even read")
eq(ivar(:@screen), :browse, "an outsized cart never reaches the confirm screen")
eq(ivar(:@message).nil?, false, "and says why")
eq(exists?("v2/apps/big.rb"), false, "nothing was written")

# ------------------------------------------- a cart whose name is hostile

group("hostile name: cannot reach outside the apps dir")
app.open_root("v2/carts")
app.inspect_cart({ name: "../../../../etc/cron.cart", path: "v2/carts/games/demo.cart",
                   dir: false, size: 14 })
eq(ivar(:@spec)[:rb_path], "v2/apps/cron.rb", "a traversal filename still installs inside the apps dir")
app.install
eq(exists?("v2/apps/cron.rb"), true, "it installed under its sanitised name")
eq(exists?("../etc/cron.rb"), false, "and nothing was written outside the tree")

puts ""
puts $fails == 0 ? "ALL PASS" : "#{$fails} FAILURE(S)"
