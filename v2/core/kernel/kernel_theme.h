#ifndef ACID_KERNEL_THEME_H
#define ACID_KERNEL_THEME_H

/* RaveOS v1's own documented palette (docs/BUILD_LOG.md), reused verbatim so
 * acid OS v2 visually continues v1's identity. These are already correct
 * 24-bit RGB hex values (e.g. #00ff66) -- unlike bring-up's hello.rb, which
 * used RGB565-style literals (0xF800/0x07E0) that this codebase's
 * gfx_fill_rect/hal_display_fill_rect chain (verified during bring-up's
 * final review to treat a bare color argument as RGB888) rendered
 * incorrectly. These values need no such translation. */
#define THEME_BG      0x050607u  /* --bg: desktop/page background */
#define THEME_HARD    0x00FF66u  /* --hard: pure acid green -- accent, borders, pressed */
#define THEME_PANEL   0x0B1712u  /* --panel: window body, brightened for a flat renderer */
#define THEME_TEXT    0xD4E6DBu  /* --text */
#define THEME_MUTED   0x9DAAA3u  /* --muted */
/* RaveOS v1's own purple (docs/BUILD_LOG.md: "EDITOR gets purple
 * (0xB026FF)"), reused here rather than picking a fresh violet, for
 * the same reason the five colors above are v1's verbatim. Used by
 * file_manager.rb to mark .app.toml manifests -- the entries that
 * launch an app when clicked -- apart from the plain files around
 * them. */
#define THEME_VIOLET  0xB026FFu  /* launchable/executable accent */

#endif
