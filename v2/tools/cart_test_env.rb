# Test environment for the cart loader's file-touching half, loaded
# BEFORE apps/cart.rb (see test_cart_install.rb for the command). Two
# jobs, both of which have to happen before CartApp exists:
#
#   1. Stub the acid_* bindings and the AcidApp/AcidKeys the vm_host
#      normally preloads, so the app class can be instantiated under a
#      plain host mruby with no OS underneath it. Drawing stubs do
#      nothing; the launcher stubs record what they were called with,
#      since "did it register the right thing" is a real assertion.
#   2. Build a throwaway fixture tree under /tmp and chdir into it, so
#      the relative paths the app uses ("v2/apps", "v2/carts") resolve
#      to fixtures instead of the real repo. The app writes real files
#      during these tests -- that is the point -- and none of them are
#      inside the repo.
ROOT = "/tmp/acidos-cart-test"

def rm_rf(path)
  begin
    d = Dir.open(path)
  rescue
    begin
      File.delete(path)
    rescue
    end
    return
  end
  names = []
  while (ent = d.read)
    names << ent unless ent == "." || ent == ".."
  end
  d.close
  names.each { |n| rm_rf("#{path}/#{n}") }
  begin
    Dir.delete(path)
  rescue
  end
end

def mkdir_p(path)
  parts = path.split("/")
  acc = ""
  parts.each do |part|
    next if part.empty?
    acc = "#{acc}/#{part}"
    begin
      Dir.mkdir(acc)
    rescue
    end
  end
end

def put_file(path, text)
  f = File.open(path, "w")
  f.write(text)
  f.close
end

rm_rf(ROOT)
mkdir_p("#{ROOT}/v2/apps/lib")
mkdir_p("#{ROOT}/v2/carts/games")
mkdir_p("#{ROOT}/elsewhere")

put_file("#{ROOT}/v2/apps/lib/acid_palette.rb", "module AcidPalette\nend\n")
put_file("#{ROOT}/v2/apps/lib/acid_game.rb", "class AcidGame\nend\n")

# The two shapes of built-in app a cart must never overwrite: one with a
# manifest (tetris), and one with none at all (desktop -- it isn't
# launchable from the Menu it draws, so it has no .app.toml).
put_file("#{ROOT}/v2/apps/desktop.rb", "# the real desktop\n")
put_file("#{ROOT}/v2/apps/tetris.rb", "# the real tetris\n")
put_file("#{ROOT}/v2/apps/tetris.app.toml", "name = Tetris\nw = 160\nh = 160\n")

HELLO_CART = "# name: Hello Acid\n# w: 200\n# h: 150\n# desc: Example cart\n" \
             "# libs: lib/acid_palette.rb, ../../etc/passwd.rb\n\nclass HelloApp\nend\n"
put_file("#{ROOT}/v2/carts/hello.cart", HELLO_CART)
# Same display name as hello.cart, different filename -- for the name
# collision the confirm screen warns about.
put_file("#{ROOT}/v2/carts/impostor.cart", "# name: Hello Acid\n\nclass Impostor\nend\n")
put_file("#{ROOT}/v2/carts/desktop.cart", "# name: Not The Desktop\n\nclass Evil\nend\n")
put_file("#{ROOT}/v2/carts/tetris.cart", "# name: Not Tetris\n\nclass Evil\nend\n")
put_file("#{ROOT}/v2/carts/big.cart", "#" * (257 * 1024))
put_file("#{ROOT}/v2/carts/notes.txt", "not a cart\n")
put_file("#{ROOT}/v2/carts/games/demo.cart", "class Demo\nend\n")
put_file("#{ROOT}/elsewhere/secret.cart", "class Secret\nend\n")
begin
  File.symlink("#{ROOT}/elsewhere", "#{ROOT}/v2/carts/escape")
rescue
end

Dir.chdir(ROOT)

# ------------------------------------------------------- binding stubs

$spawned = []
$registered = []

module Kernel
  def acid_clear_user_area; end
  def acid_draw_window_frame(title); end
  def acid_draw_window_border; end
  def acid_fill_rect(x, y, w, h, color); end
  def acid_draw_text(text, x, y, fg, bg); end
  def acid_am_i_focused; true; end

  def acid_launcher_count
    $registered.length
  end

  def acid_launcher_path(i)
    entry = $registered[i]
    entry && entry[0]
  end

  def acid_launcher_name(i)
    entry = $registered[i]
    entry && entry[1]
  end

  def acid_launcher_register(path, name, w, h, multi, libs)
    $registered << [ path, name, w, h, multi, libs ]
    true
  end

  def acid_spawn_app(path, w, h, arg)
    $spawned << [ path, w, h, arg ]
    true
  end
end

class AcidApp
  def on_create; end
  def on_touch(x, y, pressed); end
  def on_key(code, pressed); end
  def on_idle; end
  def on_destroy; end
  def redraw; end
  def focused?; true; end
  def quit!; end
  def poll_timeout_ms; 200; end
  # The real one runs the event loop; here the app under test is driven
  # method by method instead, so loading cart.rb must not block.
  def start; end
end

module AcidKeys
  UP = 1
  DOWN = 2
  ENTER = 3
  ESCAPE = 4
  BACKSPACE = 5
end
