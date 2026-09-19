#ifndef ACID_WALLPAPER_H
#define ACID_WALLPAPER_H

/* Draws the desktop wallpaper onto the real screen (see wallpaper.c). The
 * router's compositor calls this in place of the old flat gfx_clear_screen
 * background clear. */
void wallpaper_blit( void );

/* Replays the wallpaper's own pixels for just the given rect into an
 * arbitrary caller-owned canvas, at the same absolute coordinates the real
 * screen would show there. For a window (like desktop.rb) that owns a
 * region of screen it isn't currently using for its own content, painting
 * the actual wallpaper pixels into that part of its own canvas is how this
 * codebase gets a "shows the background" look without real alpha
 * compositing (gfx_blit_canvas is a plain opaque copy -- see gfx.h). Cheap
 * enough for a one-off UI action (a few thousand run comparisons), not
 * meant to run every frame. */
void wallpaper_draw_into( void * canvas, int x, int y, int w, int h );

/* Config's on/off toggle (see window_binding.c's acid_set_wallpaper_enabled/
 * acid_get_wallpaper_enabled) -- disabled falls both of the above back to a
 * flat THEME_BG fill, same as before the wallpaper existed. */
void wallpaper_set_enabled( int enabled );
int wallpaper_is_enabled( void );

#endif
