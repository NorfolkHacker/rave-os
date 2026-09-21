# Load Cart -- install a program from a card.
#
# A .cart is plain Ruby source living OUTSIDE this OS: on a USB stick, an
# SD card, a directory on the host. This app browses the handful of places
# those turn up, shows what a cart says about itself, and -- once you
# confirm -- copies it into v2/apps as <slug>.rb with a generated
# <slug>.app.toml beside it, which is exactly the shape desktop.rb's boot
# scan already registers. That's the whole point: an app can now be
# written, edited and versioned outside this repo and carried in, instead
# of having to be born in v2/apps.
#
# This replaces the App Store, which browsed the apps already installed
# and had nowhere to install anything FROM. Its one real feature -- the
# MENU/HIDDEN toggle rewriting an app's `menu =` line -- is gone with it;
# Editor opens any manifest and that line is one word.
#
# Where the carts come from: CART_ROOTS, below. Only those trees are
# reachable -- this is a cart loader, not a general host-filesystem
# browser, and "the places a card gets mounted" is the list that will
# grow a real SD mount point when phase 6 puts this on hardware.
#
# What this app checks, and what it doesn't: every decision about the
# DESTINATION -- the filename, the window size, which modules load,
# whether that slot may be written at all -- goes through cart/cartfile.rb
# (read its header comment) and nothing here builds a path by hand. What
# nothing here can check is the cart's own CODE: once installed, it runs
# in its own VM task with the same bindings every built-in app has. A cart
# is trusted code you chose to carry in, exactly like a program copied
# onto any other machine.
class CartApp < AcidApp
  # Must match cart.app.toml.
  WINDOW_W = 300
  WINDOW_H = 210
  TITLE_BAR_H = 16      # kernel_layout.h's KERNEL_TITLE_BAR_H
  HEADER_H = 12
  ROW_H = 12
  BTN_H = 16
  BTN_W = 84
  BTN_GAP = 8
  FOOTER_H = BTN_H + 8

  BG_COLOR = 0x050607      # THEME_BG
  PANEL_COLOR = 0x0B1712   # THEME_PANEL -- header/footer strips
  TEXT_COLOR = 0xD4E6DB    # THEME_TEXT
  MUTED_COLOR = 0x9DAAA3   # THEME_MUTED
  HARD_COLOR = 0x00FF66    # THEME_HARD -- accent/selection only
  ALERT_COLOR = 0xB026FF   # THEME_VIOLET -- refusals and warnings
  SEL_BG = 0x123322        # THEME_PANEL's documented button-hover shade

  # Where apps live, and where their shared modules live. Both spellings
  # of the apps dir agree with Cartfile::APPS_DIR by construction -- the
  # module builds every destination path itself.
  LIB_DIR = "v2/apps/lib"

  # The only trees this app will browse. First the repo's own carts
  # directory (so the sim has somewhere real to load from with no card
  # plugged in at all), then the user's ~/carts, then the three places a
  # removable volume actually gets mounted on a Linux host. On real
  # hardware this is the one line that grows the SD mount point -- the
  # app above it doesn't care which root a cart came from.
  FIXED_ROOTS = [ "/media", "/mnt", "/run/media" ]
  REPO_ROOT = "v2/carts"

  def on_create
    @touch_held = false
    @message = nil
    @roots = existing_roots
    @entries = []
    @selected = 0
    @scroll = 0
    @spec = nil
    @cart_text = nil
    @status = nil
    @name_clash = nil
    @screen = :roots
  end

  def window_title
    "Load Cart"
  end

  # ------------------------------------------------------------ roots

  # ENV isn't guaranteed to exist in every mruby build this OS is linked
  # against (the hw target's gembox is far smaller than the sim's), so a
  # missing ENV means "no home root", not a crash on boot.
  def home_carts
    return nil unless defined?(ENV)
    home = ENV["HOME"]
    return nil if home.nil? || home.empty?
    "#{home}/carts"
  rescue
    nil
  end

  def candidate_roots
    roots = [ REPO_ROOT ]
    hc = home_carts
    roots << hc if hc
    roots + FIXED_ROOTS
  end

  # Only the roots that are really there -- an empty list is its own
  # legible screen ("no cards found"), better than offering four paths
  # that all fail to open when tapped.
  def existing_roots
    list = []
    candidate_roots.each { |p| list << p if dir?(p) }
    list
  end

  def dir?(path)
    d = Dir.open(path)
    d.close
    true
  rescue
    false
  end

  # A symlinked entry is skipped rather than followed: the browser's
  # containment (Cartfile.parent_dir/child_dir) is path arithmetic, so a
  # symlink inside a cart root pointing at /etc would silently hand this
  # app a tree outside every root it's allowed in. Skipping costs a
  # developer one `cp` instead of a `ln -s`; following would quietly make
  # the root list mean nothing.
  def symlink?(path)
    File.symlink?(path)
  rescue
    false
  end

  def size_of(path)
    File.size(path)
  rescue
    0
  end

  def read_text(path)
    f = File.open(path, "r")
    text = f.read
    f.close
    text
  rescue
    nil
  end

  # Modules a cart may ask for, as the manifest spells them
  # ("lib/acid_game.rb") -- the real contents of apps/lib, read fresh so
  # this never drifts from what's actually installable. Cartfile.filter_libs
  # keeps only entries in this list, which is what stops a `# libs:` line
  # naming a path of its own.
  def available_libs
    libs = []
    d = Dir.open(LIB_DIR)
    while (ent = d.read)
      libs << "lib/#{ent}" if ent.end_with?(".rb")
    end
    d.close
    libs
  rescue
    []
  end

  # ----------------------------------------------------------- browsing

  def open_root(root)
    @root = root
    @dir = root
    @message = nil
    scan_dir
    @screen = :browse
  end

  # Directories and .cart files only. Everything else on the card is
  # invisible here -- this app has no business showing the contents of a
  # stranger's USB stick, and a cart is the only thing it can act on.
  def scan_dir
    @entries = []
    @selected = 0
    @scroll = 0
    up = Cartfile.parent_dir(@dir, @root)
    @entries << { name: "..", dir: true, path: up } if up
    begin
      d = Dir.open(@dir)
      names = []
      while (ent = d.read)
        names << ent unless ent == "." || ent == ".."
      end
      d.close
      names.sort.each do |name|
        path = Cartfile.child_dir(@dir, name)
        next if path.nil?
        next if symlink?(path)
        if dir?(path)
          @entries << { name: name, dir: true, path: path }
        elsif Cartfile.cart?(name)
          @entries << { name: name, dir: false, path: path, size: size_of(path) }
        end
      end
    rescue
      @message = "cannot read this directory"
    end
  end

  def visible_rows
    (WINDOW_H - TITLE_BAR_H - HEADER_H - FOOTER_H) / ROW_H
  end

  def ensure_scroll
    if @selected < @scroll
      @scroll = @selected
    elsif @selected >= @scroll + visible_rows
      @scroll = @selected - visible_rows + 1
    end
  end

  def row_count
    @screen == :roots ? @roots.length : @entries.length
  end

  # ------------------------------------------------------------ opening

  def open_selected
    if @screen == :roots
      root = @roots[@selected]
      return if root.nil?
      open_root(root)
      redraw
      return
    end
    entry = @entries[@selected]
    return if entry.nil?
    if entry[:dir]
      @dir = entry[:path]
      @message = nil
      scan_dir
      redraw
    else
      inspect_cart(entry)
    end
  end

  # Reads the cart and works out what installing it would mean, without
  # writing anything. Everything the confirm screen shows comes from
  # Cartfile.from_cart -- including the destination paths, so what's on
  # screen is literally what gets written.
  def inspect_cart(entry)
    bytes = entry[:size]
    unless Cartfile.size_ok?(bytes)
      @message = bytes > 0 ? "too big: #{bytes}B (max #{Cartfile::MAX_BYTES})" : "empty file"
      redraw
      return
    end
    text = read_text(entry[:path])
    if text.nil?
      @message = "cannot read #{entry[:name]}"
      redraw
      return
    end
    spec = Cartfile.from_cart(entry[:name], text, available_libs)
    if spec.nil?
      @message = "no usable app name in #{entry[:name]}"
      redraw
      return
    end
    @spec = spec
    @spec[:source] = entry[:path]
    @spec[:bytes] = bytes
    @cart_text = text
    @status = destination_status(spec)
    @name_clash = Cartfile.name_clash(spec[:name], spec[:rb_path], registry_entries)
    @message = nil
    @screen = :confirm
    redraw
  end

  def destination_status(spec)
    Cartfile.destination_status(file_exists?(spec[:rb_path]),
                                read_manifest(spec[:toml_path]))
  end

  def file_exists?(path)
    f = File.open(path, "r")
    f.close
    true
  rescue
    false
  end

  # nil means "no manifest there", which destination_status reads
  # differently from an empty one -- see its own comment.
  def read_manifest(path)
    text = read_text(path)
    return nil if text.nil?
    fields = {}
    text.split("\n").each do |line|
      line = line.strip
      next if line.empty? || line.start_with?("#")
      eq = line.index("=")
      next unless eq
      fields[line[0, eq].strip] = line[eq + 1, line.length - eq - 1].strip
    end
    fields
  end

  # ---------------------------------------------------------- installing

  def install
    return if @spec.nil? || @cart_text.nil?
    if @status == :protected
      @message = "that slot belongs to a built-in app"
      redraw
      return
    end
    unless write_file(@spec[:rb_path], @cart_text)
      @message = "could not write #{@spec[:rb_path]}"
      redraw
      return
    end
    manifest = Cartfile.manifest_text(@spec)
    unless write_file(@spec[:toml_path], manifest)
      # The source landed but the manifest didn't, so nothing will launch
      # it -- say so plainly rather than reporting a success that leaves
      # a half-installed app behind.
      @message = "wrote source but not manifest -- not installed"
      redraw
      return
    end
    @message = nil
    @screen = :done
    redraw
  end

  def write_file(path, text)
    f = File.open(path, "w")
    f.write(text)
    f.close
    true
  rescue
    false
  end

  # Registers the freshly installed cart with the C-side launcher so it
  # can run right now, then spawns it. The Menu dropdown itself only
  # rebuilds at boot (desktop.rb builds @menu_visible once in its own
  # on_create), the same limitation the App Store documented before this
  # -- so the cart appears in the Menu on the next boot, and this button
  # is how you play it in the meantime.
  def launch
    return if @spec.nil?
    register_spec unless registered?(@spec[:rb_path])
    acid_spawn_app(@spec[:rb_path], @spec[:w], @spec[:h], "")
  end

  # The launcher registry as Cartfile.name_clash wants it -- every app
  # registered at boot (Menu-visible or not), plus anything installed
  # since.
  def registry_entries
    entries = []
    i = 0
    count = acid_launcher_count
    while i < count
      entries << [ acid_launcher_path(i), acid_launcher_name(i) ]
      i += 1
    end
    entries
  end

  def registered?(path)
    i = 0
    count = acid_launcher_count
    while i < count
      return true if acid_launcher_path(i) == path
      i += 1
    end
    false
  end

  def register_spec
    acid_launcher_register(@spec[:rb_path], @spec[:name], @spec[:w], @spec[:h],
                           false, @spec[:libs].join(", "))
  end

  # ------------------------------------------------------------ drawing

  def redraw
    acid_clear_user_area
    acid_draw_window_frame(window_title)
    case @screen
    when :roots   then draw_list("CARDS", @roots.length, [ "OPEN" ])
    when :browse  then draw_list(@dir, @entries.length, [ "OPEN", "CARDS" ])
    when :confirm then draw_confirm
    when :done    then draw_done
    end
    acid_draw_window_border
  end

  def draw_header(label)
    acid_fill_rect(0, TITLE_BAR_H, WINDOW_W, HEADER_H, PANEL_COLOR)
    acid_draw_text(label[0, 46], 2, TITLE_BAR_H + 2, MUTED_COLOR, PANEL_COLOR)
  end

  def draw_list(label, count, buttons)
    draw_header(count > visible_rows ? "#{label} (#{@selected + 1}/#{count})" : label)
    y = TITLE_BAR_H + HEADER_H
    if count == 0
      acid_fill_rect(0, y, WINDOW_W, ROW_H, BG_COLOR)
      acid_draw_text(empty_text, 2, y + 2, MUTED_COLOR, BG_COLOR)
    end
    i = @scroll
    while i < count && i < @scroll + visible_rows
      row_bg = (i == @selected) ? SEL_BG : BG_COLOR
      acid_fill_rect(0, y, WINDOW_W, ROW_H, row_bg)
      acid_draw_text(row_text(i)[0, 46], 2, y + 2, row_color(i), row_bg)
      y += ROW_H
      i += 1
    end
    draw_footer(buttons)
  end

  def empty_text
    @screen == :roots ? "no cards found -- see CART_ROOTS in cart.rb" : "no carts in this directory"
  end

  def row_text(i)
    return @roots[i] if @screen == :roots
    e = @entries[i]
    e[:dir] ? "[#{e[:name]}]" : " #{e[:name]} (#{e[:size]}B)"
  end

  def row_color(i)
    return HARD_COLOR if @screen == :roots
    e = @entries[i]
    e[:dir] ? HARD_COLOR : TEXT_COLOR
  end

  def draw_confirm
    spec = @spec
    draw_header("INSTALL CART")
    y = TITLE_BAR_H + HEADER_H
    y = draw_field(y, "name", spec[:name])
    y = draw_field(y, "size", "#{spec[:w]}x#{spec[:h]}  #{spec[:bytes]}B")
    y = draw_field(y, "desc", spec[:desc].empty? ? "(none)" : spec[:desc])
    y = draw_field(y, "libs", spec[:libs].empty? ? "(none)" : spec[:libs].join(", "))
    y = draw_field(y, "from", spec[:source])
    y = draw_field(y, "into", spec[:rb_path])
    acid_fill_rect(0, y, WINDOW_W, ROW_H, BG_COLOR)
    acid_draw_text(status_text[0, 46], 2, y + 2, status_color, BG_COLOR)
    unless @name_clash.nil?
      y += ROW_H
      acid_fill_rect(0, y, WINDOW_W, ROW_H, BG_COLOR)
      acid_draw_text("name already answers to #{@name_clash}"[0, 46], 2, y + 2, ALERT_COLOR, BG_COLOR)
    end
    draw_footer(@status == :protected ? [ "BACK" ] : [ confirm_label, "BACK" ])
  end

  def draw_field(y, label, value)
    acid_fill_rect(0, y, WINDOW_W, ROW_H, BG_COLOR)
    acid_draw_text(label, 2, y + 2, MUTED_COLOR, BG_COLOR)
    acid_draw_text(value.to_s[0, 40], 40, y + 2, TEXT_COLOR, BG_COLOR)
    y + ROW_H
  end

  def confirm_label
    @status == :replace ? "REPLACE" : "INSTALL"
  end

  def status_text
    case @status
    when :replace   then "replaces the cart already installed there"
    when :protected then "REFUSED: #{@spec[:slug]} is a built-in app"
    else                 "new install"
    end
  end

  def status_color
    @status == :fresh ? HARD_COLOR : ALERT_COLOR
  end

  def draw_done
    draw_header("INSTALLED")
    y = TITLE_BAR_H + HEADER_H
    y = draw_field(y, "app", @spec[:name])
    y = draw_field(y, "src", @spec[:rb_path])
    y = draw_field(y, "man", @spec[:toml_path])
    acid_fill_rect(0, y, WINDOW_W, ROW_H * 2, BG_COLOR)
    acid_draw_text("joins the Menu at next boot --", 2, y + 2, MUTED_COLOR, BG_COLOR)
    acid_draw_text("RUN starts it now.", 2, y + ROW_H + 2, MUTED_COLOR, BG_COLOR)
    draw_footer([ "RUN", "CARDS" ])
  end

  # The footer carries this screen's buttons on the left and whatever the
  # last action had to say on the right -- one line, so an error never
  # pushes the buttons off screen.
  def draw_footer(buttons)
    y = footer_y
    acid_fill_rect(0, y, WINDOW_W, FOOTER_H, PANEL_COLOR)
    i = 0
    while i < buttons.length
      x = button_x(i)
      acid_fill_rect(x, y + 4, BTN_W, BTN_H, SEL_BG)
      acid_draw_text(buttons[i], x + 4, y + 4 + BTN_H / 2 - 4, HARD_COLOR, SEL_BG)
      i += 1
    end
    return if @message.nil?
    acid_draw_text(@message[0, 24], button_x(buttons.length) + 4, y + 8, ALERT_COLOR, PANEL_COLOR)
  end

  def footer_y
    WINDOW_H - FOOTER_H
  end

  def button_x(i)
    2 + i * (BTN_W + BTN_GAP)
  end

  # Which footer button a tap landed on, or nil. Mirrors draw_footer's own
  # geometry -- the two are kept together deliberately.
  def button_at(x, y, count)
    return nil if y < footer_y + 4 || y > footer_y + 4 + BTN_H
    i = 0
    while i < count
      bx = button_x(i)
      return i if x >= bx && x < bx + BTN_W
      i += 1
    end
    nil
  end

  def footer_buttons
    case @screen
    when :roots   then [ "OPEN" ]
    when :browse  then [ "OPEN", "CARDS" ]
    when :confirm then (@status == :protected ? [ "BACK" ] : [ confirm_label, "BACK" ])
    when :done    then [ "RUN", "CARDS" ]
    else               []
    end
  end

  # ------------------------------------------------------------- input

  def on_touch(x, y, pressed)
    # Same one-press-per-hold discipline as every other tap-to-act app in
    # this OS (file_manager.rb, sysmon.rb, the App Store this replaced):
    # the router resends TOUCH every ~16ms while held, and without this
    # guard one tap-and-hold on INSTALL would run the install over and
    # over for as long as a finger stayed down.
    unless pressed
      @touch_held = false
      return
    end
    return if @touch_held
    @touch_held = true

    buttons = footer_buttons
    btn = button_at(x, y, buttons.length)
    if btn
      press_button(buttons[btn])
      return
    end
    return if @screen == :confirm || @screen == :done

    top = TITLE_BAR_H + HEADER_H
    return if y < top || y >= footer_y
    row = (y - top) / ROW_H + @scroll
    return if row < 0 || row >= row_count
    @selected = row
    open_selected
  end

  def press_button(label)
    case label
    when "OPEN"            then open_selected
    when "CARDS"           then back_to_roots
    when "BACK"            then back_to_browse
    when "INSTALL", "REPLACE" then install
    when "RUN"             then launch
    end
  end

  def back_to_roots
    @roots = existing_roots
    @screen = :roots
    @selected = 0
    @scroll = 0
    @message = nil
    redraw
  end

  def back_to_browse
    @screen = :browse
    @spec = nil
    @cart_text = nil
    @name_clash = nil
    @message = nil
    redraw
  end

  def on_key(code, pressed)
    return unless pressed
    if @screen == :confirm
      if code == AcidKeys::ENTER
        install
      elsif code == AcidKeys::ESCAPE || code == AcidKeys::BACKSPACE
        back_to_browse
      end
      return
    end
    if @screen == :done
      if code == AcidKeys::ENTER
        launch
      elsif code == AcidKeys::ESCAPE || code == AcidKeys::BACKSPACE
        back_to_roots
      end
      return
    end
    if code == AcidKeys::UP
      @selected -= 1 if @selected > 0
      ensure_scroll
      redraw
    elsif code == AcidKeys::DOWN
      @selected += 1 if @selected < row_count - 1
      ensure_scroll
      redraw
    elsif code == AcidKeys::ENTER
      open_selected
    elsif code == AcidKeys::BACKSPACE || code == AcidKeys::ESCAPE
      go_up
    end
  end

  # BACKSPACE walks back out: up one directory, out to the card list at
  # the root, and no further.
  def go_up
    return if @screen == :roots
    up = Cartfile.parent_dir(@dir, @root)
    if up.nil?
      back_to_roots
    else
      @dir = up
      @message = nil
      scan_dir
      redraw
    end
  end
end

CartApp.new.start
