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

  # The editor's own source, plus acid_app.rb (every app loads it, so a
  # bad save there bricks every app, the editor included) -- task 12's
  # "the editor must not be able to break itself" set. Both save_file
  # (EditorApp) and cmd_run_file (EditorCmd, a mixin) need this, and a
  # mixin's methods only resolve a bare constant through its OWN nesting
  # and ancestry, never through whatever class includes it -- see the
  # comment above on why this module exists. Living here, in the one
  # module both sides already include, is what makes it visible to both.
  #
  # Suffixes, not full paths, matched with end_with?: the same file is
  # reachable both as v2/apps/editor.rb and, through the fsroot/App
  # symlink to v2/apps, as v2/fsroot/App/editor.rb, and a suffix match
  # recognises both with no need to resolve the symlink -- there's no
  # realpath binding in this mruby, and hardcoding a comparison that only
  # works on the sim's real filesystem while doing nothing on the
  # hardware target's stub FS would be worse than this explicit list.
  # Each entry leads with "/" so "editor.rb" doesn't also match some
  # unrelated file that merely ends in those letters, e.g. "xeditor.rb".
  OWN_SOURCE_SUFFIXES = [
    "/editor.rb",
    "/editor/buffer.rb",
    "/editor/hl.rb",
    "/editor/cmdbar.rb",
    "/editor/layout.rb",
    "/editor/touch.rb",
    "/lib/acid_app.rb",
  ]

  def own_source?(path)
    OWN_SOURCE_SUFFIXES.any? { |suffix| path.end_with?(suffix) }
  end
end
