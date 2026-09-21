Carts -- programs written outside acid OS and carried in.

A .cart is a plain Ruby app (see hello_acid.cart) that the Load Cart app
installs into v2/apps as <slug>.rb plus a generated <slug>.app.toml. This
directory is one of the roots Load Cart browses; the others are ~/carts
and the host's /media, /mnt and /run/media mount points, so a cart on a
USB stick or SD card shows up in the same list. On real hardware the
memory card's own mount point joins that list.

Header (all lines optional, first comment block only):

  # name: Hello Acid            window title and Menu entry
  # w: 200                      window width,  clamped to the screen
  # h: 150                      window height, clamped to the screen
  # desc: one-line description  shown in the Menu and File Manager
  # libs: lib/acid_palette.rb   modules from v2/apps/lib, comma separated

An installed cart joins the Menu dropdown at the next boot; Load Cart's
RUN button starts it immediately in the meantime.
