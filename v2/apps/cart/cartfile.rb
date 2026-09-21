# Everything the cart loader decides BEFORE it touches a file -- pure
# string work, no acid_* bindings, no IO, so tools/test_cart.rb can run
# the whole lot under the host mruby (same split as editor/buffer.rb).
#
# A .cart is just Ruby source on a host card. Installing one means writing
# it into v2/apps as <slug>.rb plus a generated <slug>.app.toml, which is
# exactly the point where a hostile (or merely careless) cart could try to
# reach somewhere it shouldn't. That's what this module is: the filename
# becomes a slug that CANNOT contain a path, the header's `libs` is an
# allow-list membership test rather than a path, the window size is
# clamped to the real screen, and every header value has its control
# characters stripped so a value can never inject a second manifest line.
#
# What it deliberately does NOT do is sandbox the cart's code. An
# installed cart runs in its own VM task with the same bindings every
# built-in app has -- it can draw, make noise, read and write fsroot, and
# spawn other apps. A cart is trusted code you chose to install, the same
# way a program you copy onto any machine is; the checks here protect the
# OS's own files from a malicious NAME, not from a malicious PROGRAM.
module Cartfile
  CART_EXT = ".cart"

  # Where an installed cart lands -- the same directory desktop.rb scans
  # for <name>.app.toml manifests at boot, which is what makes a cart
  # show up in the Menu on the next one.
  APPS_DIR = "v2/apps"

  # Refuse anything that isn't plausibly a hand-written program. 256 KB is
  # far past any app in v2/apps and still small enough to read into a
  # String on the target's heap.
  MAX_BYTES = 256 * 1024

  MAX_SLUG = 24    # destination filename length, sans extension
  MAX_TEXT = 40    # longest header value kept (name, desc)
  MAX_LIBS = 8     # most modules one cart may pull in

  # Window bounds, from kernel_layout.h: KERNEL_SCREEN_W/H (640x360) less
  # the desktop strip. A cart asking for something outside this gets
  # clamped rather than refused -- a bad number is a typo, not an attack,
  # but an unclamped one is a window nobody can reach the title bar of.
  MIN_W = 80
  MAX_W = 640
  MIN_H = 48
  MAX_H = 336
  DEFAULT_W = 220
  DEFAULT_H = 160

  # The only header keys that mean anything. Anything else in the comment
  # block is a comment, including keys we might add later -- an unknown
  # key never reaches the generated manifest.
  HEADER_KEYS = [ "name", "w", "h", "desc", "libs" ]

  # True for host files the browser should even offer. ".cart" on its own
  # has no stem to make a filename out of, so it isn't one.
  def self.cart?(name)
    return false if name.nil?
    return false unless cart_ext?(name)
    name.length > CART_EXT.length
  end

  def self.cart_ext?(name)
    return false if name.nil? || name.length < CART_EXT.length
    tail = name[name.length - CART_EXT.length, CART_EXT.length]
    tail.downcase == CART_EXT
  end

  # The destination filename, from the host file's name. This is the
  # security-critical one, so it works by construction rather than by
  # blacklist: only [a-z0-9_] survive, everything else collapses to a
  # single underscore, so there is no input -- "../../desktop.cart",
  # "/etc/passwd.cart", a name with an embedded newline -- that can
  # produce a slug containing a path separator, a traversal component or
  # a line break. nil means "no usable name", which the caller reports
  # rather than guessing at.
  def self.slug(filename)
    return nil if filename.nil?
    base = basename(filename)
    base = base[0, base.length - CART_EXT.length] if cart_ext?(base)

    out = ""
    underscore = false
    i = 0
    while i < base.length
      ch = base[i].downcase
      if ( ch >= "a" && ch <= "z" ) || ( ch >= "0" && ch <= "9" )
        out << ch
        underscore = false
      elsif !underscore
        out << "_"
        underscore = true
      end
      i += 1
    end

    out = trim_underscores(out)
    return nil if out.empty?
    # Truncating can expose a trailing underscore that wasn't trailing
    # before, so trim again rather than installing "my_long_name_".
    trimmed = trim_underscores(out[0, MAX_SLUG])
    trimmed.empty? ? nil : trimmed
  end

  def self.basename(path)
    idx = path.rindex("/")
    idx.nil? ? path : path[idx + 1, path.length - idx - 1]
  end

  def self.trim_underscores(s)
    s = s[1, s.length - 1] while s.length > 0 && s[0] == "_"
    s = s[0, s.length - 1] while s.length > 0 && s[s.length - 1] == "_"
    s
  end

  # The leading `# key: value` comment block. Reading stops at the first
  # line that isn't blank and isn't a comment -- i.e. at the cart's first
  # line of real code -- so a `# name:` further down (in a comment inside
  # the program, say) is never mistaken for metadata. First value wins.
  def self.parse_header(text)
    fields = {}
    return fields if text.nil?
    text.split("\n").each do |raw|
      line = raw.strip
      next if line.empty?
      break unless line[0] == "#"
      body = line[1, line.length - 1].strip
      colon = body.index(":")
      next if colon.nil?
      key = body[0, colon].strip.downcase
      next unless HEADER_KEYS.include?(key)
      next if fields[key]
      value = clean_text(body[colon + 1, body.length - colon - 1])
      fields[key] = value unless value.empty?
    end
    fields
  end

  # Header values end up as manifest lines and as on-screen text, so
  # anything outside printable ASCII becomes a space: that's what stops a
  # value carrying a newline (which would inject a whole extra manifest
  # key) or a control code the 8x8 font has no glyph for.
  def self.clean_text(s)
    out = ""
    i = 0
    while i < s.length
      ch = s[i]
      b = ch.bytes[0]
      out << ( ( b < 32 || b > 126 ) ? " " : ch )
      i += 1
    end
    out.strip[0, MAX_TEXT]
  end

  # A cart's requested window edge, clamped into what the screen can
  # actually show. A missing/blank value takes the caller's default; a
  # non-numeric one reads as 0 and clamps up to the minimum, so the
  # result is never a zero-size window.
  def self.dimension(raw, fallback, min, max)
    return fallback if raw.nil? || raw.strip.empty?
    v = raw.to_i
    return min if v < min
    return max if v > max
    v
  end

  # Modules the cart asked for, kept only if they're in `available` (the
  # real contents of apps/lib, passed in by the caller). Membership, not
  # path validation: "../../../etc/passwd.rb" isn't in the list, so it's
  # dropped without this needing to reason about traversal at all. The
  # loaded-module list is what vm_host feeds to the VM, so this is the
  # one header field with teeth.
  def self.filter_libs(raw, available)
    return [] if raw.nil?
    out = []
    raw.split(",").each do |entry|
      name = entry.strip
      next if name.empty?
      next unless available.include?(name)
      next if out.include?(name)
      out << name
      break if out.length >= MAX_LIBS
    end
    out
  end

  # The generated <slug>.app.toml. `source = cart` is load-bearing: it's
  # how a later install knows this slot was installed from a cart and may
  # be replaced, and how every hand-written app in v2/apps is recognised
  # as off-limits (see replaceable?).
  def self.manifest_text(fields)
    lines = []
    lines << "name = #{fields[:name]}"
    lines << "w = #{fields[:w]}"
    lines << "h = #{fields[:h]}"
    desc = fields[:desc]
    lines << "desc = #{desc}" if desc && !desc.empty?
    libs = fields[:libs]
    lines << "libs = #{libs.join(", ")}" if libs && !libs.empty?
    lines << "source = cart"
    lines.join("\n") + "\n"
  end

  # Whether a cart may write over the manifest already in that slot. nil
  # (nothing there) is a fresh install; anything without `source = cart`
  # is a built-in app and is refused outright rather than confirmed --
  # a cart called "desktop.cart" must not be able to replace the desktop,
  # even by a user tapping through a prompt.
  def self.replaceable?(fields)
    return true if fields.nil?
    fields["source"] == "cart"
  end

  # What may be done to the <slug> slot in v2/apps, given whether
  # <slug>.rb is already there and whatever <slug>.app.toml parsed to
  # (nil = no manifest). Three answers, and the caller only ever offers
  # an install for the first two.
  #
  # The bare-.rb case is the one worth spelling out: apps/desktop.rb has
  # no manifest of its own (the desktop isn't launchable from the Menu it
  # draws), so a manifest lookup alone would read that slot as empty and
  # happily let a cart called "desktop.cart" overwrite the desktop. An
  # existing .rb we didn't install is protected whether or not anything
  # documents it.
  def self.destination_status(rb_exists, manifest_fields)
    return :fresh if !rb_exists && manifest_fields.nil?
    return :replace if rb_exists && replaceable?(manifest_fields) && !manifest_fields.nil?
    :protected
  end

  # The already-registered app a cart's name would collide with, or nil.
  # `entries` is [[path, name], ...] straight from the launcher registry.
  #
  # This is deliberately only a warning's worth of information, not a
  # refusal: two apps may legitimately want the same display name, and
  # the files are protected either way (see destination_status). What it
  # prevents is the quiet case -- a cart installing to a free slot under
  # a built-in's name and, because Terminal's `run` takes the first
  # case-insensitive match in registry order and a cart picks its own
  # filename, answering to that name first. An app never collides with
  # itself, so replacing a cart in place is not a clash.
  def self.name_clash(name, rb_path, entries)
    target = name.downcase
    entries.each do |entry|
      next if entry[0] == rb_path
      return entry[0] if entry[1] && entry[1].downcase == target
    end
    nil
  end

  def self.size_ok?(bytes)
    bytes > 0 && bytes <= MAX_BYTES
  end

  # Where ".." goes from `dir`, or nil if that would leave `root`. The
  # browser only ever moves by parent_dir/child_dir, so the reachable set
  # is exactly the tree under one configured cart root.
  def self.parent_dir(dir, root)
    return nil if dir == root
    idx = dir.rindex("/")
    return nil if idx.nil?
    parent = idx == 0 ? "/" : dir[0, idx]
    return nil unless parent == root || parent.start_with?("#{root}/")
    parent
  end

  # Where a listed entry name goes from `dir`. Names come from Dir#read,
  # so "." and ".." are the expected junk; a name with a separator in it
  # shouldn't be possible at all, and is refused rather than joined.
  def self.child_dir(dir, name)
    return nil if name.nil? || name.empty?
    return nil if name == "." || name == ".."
    return nil if name.include?("/")
    base = dir.end_with?("/") ? dir[0, dir.length - 1] : dir
    "#{base}/#{name}"
  end

  # Everything above, composed: the install the UI shows on its confirm
  # screen and then carries out. nil means the filename yielded no usable
  # slug, the one case with nothing sensible to install as.
  def self.from_cart(filename, text, available_libs)
    s = slug(filename)
    return nil if s.nil?
    fields = parse_header(text)
    {
      slug: s,
      name: fields["name"] || default_name(s),
      w: dimension(fields["w"], DEFAULT_W, MIN_W, MAX_W),
      h: dimension(fields["h"], DEFAULT_H, MIN_H, MAX_H),
      desc: fields["desc"] || "",
      libs: filter_libs(fields["libs"], available_libs),
      rb_path: "#{APPS_DIR}/#{s}.rb",
      toml_path: "#{APPS_DIR}/#{s}.app.toml"
    }
  end

  # "acid_snake" -> "Acid Snake", for a cart that shipped no `# name:`.
  def self.default_name(s)
    s.split("_").map { |word| word.empty? ? word : word[0].upcase + word[1, word.length - 1] }.join(" ")
  end
end
