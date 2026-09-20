# Geometry shared between EditorApp and its mixins (EditorCmd, and
# whatever Task 10's touch.rb adds).
#
# Ruby resolves a bare constant lexically: through the method's own
# module nesting, then that nesting's ancestors -- never through whatever
# class happens to include the module the method lives in. A mixin's
# methods can't see constants defined only in the class that includes the
# mixin (EditorApp including EditorCmd does NOT put EditorApp's constants
# within EditorCmd's reach), which is exactly how the first ESC in this
# editor crashed with "uninitialized constant EditorCmd::STATUS_Y". The
# fix is not to have the mixin reach into its includer; it's to give
# both sides a common module to include, so the constant is genuinely in
# both modules' own ancestry.
module EditorLayout
  # Must match editor.app.toml and kernel_layout.h's KERNEL_TITLE_BAR_H.
  # 420x280 gives 25 lines of 65 columns; the old 240x170 gave 14 of 35,
  # which is a viewer more than an editor. The screen is 640x360, so two
  # of these still fit side by side.
  WINDOW_W = 420
  WINDOW_H = 280
  TITLE_BAR_H = 16
  LINE_H = 10
  CHAR_W = 6

  # The status line sits at the bottom of the window: command mode raises
  # its strip above it, and a command surface that grows upward from the
  # bottom edge doesn't push the text you're looking at around.
  STATUS_Y = WINDOW_H - LINE_H
  TEXT_Y = TITLE_BAR_H

  GUTTER_CHARS = 4
  GUTTER_W = GUTTER_CHARS * CHAR_W
  TEXT_X = GUTTER_W + 2
end
