#ifndef RAVEOS_SYSCALL_H
#define RAVEOS_SYSCALL_H

/* Returns a fixed, recognizable value -- pure plumbing, proves a
 * syscall's argument and return value both cross the ring3/ring0
 * boundary intact. Ignores arg. */
#define SYS_TEST 0

/* Reserved syscall number for "the caller is done"; currently just
 * returns 0 and does nothing else -- this minimal ABI has no
 * process/address-space/scheduler-slot concept to actually tear down
 * (see docs/superpowers/specs/2026-08-27-ring3-syscall-design.md's
 * Out of Scope), so this is not real process termination. Ignores arg. */
#define SYS_EXIT 1

/* Reads an existing file's contents into a ring-3-owned buffer,
 * wrapping fs_read_file() (kernel/fs/fs.h) with zero changes to its
 * behavior. arg is the address of a struct sys_read_file_args built
 * by the caller -- ring3.asm's ABI still passes exactly one value in
 * ebx; for this syscall, that value is a pointer instead of a plain
 * integer. */
#define SYS_READ_FILE 2

/* Carries fs_read_file()'s four arguments across the syscall boundary
 * as a single pointer. Field types and order match fs_read_file()'s
 * own signature exactly (kernel/fs/fs.h) -- this struct exists only
 * to fit four arguments through one register, not to add or
 * reinterpret any of them. */
struct sys_read_file_args {
    const char *path;
    void *buf;
    unsigned int buf_size;
    unsigned int *out_size;
};

/* Creates a new file with the given contents, wrapping fs_create_file()
 * (kernel/fs/fs.h) with zero changes to its behavior -- including its
 * write-once semantics (fails if the path already exists). arg is the
 * address of a struct sys_create_file_args built by the caller. */
#define SYS_CREATE_FILE 3

/* Lists a directory's entries, wrapping fs_list_dir()
 * (kernel/fs/fs.h) with zero changes to its behavior. arg is the
 * address of a struct sys_list_dir_args built by the caller. */
#define SYS_LIST_DIR 4

/* Mirrors fs_create_file()'s signature exactly (kernel/fs/fs.h). */
struct sys_create_file_args {
    const char *path;
    const void *data;
    unsigned int size;
};

/* Forward-declared, not included from fs.h: only a pointer to this
 * type appears below, which needs the tag to exist, not its full
 * layout. Keeps syscall.h's own dependency footprint at zero fs.h
 * symbols, same as it already is today; kernel/arch/syscall_fs.c
 * (which already includes fs.h for fs_read_file()) gets the real
 * definition for free when it includes syscall.h. */
struct fs_dirent;

/* Mirrors fs_list_dir()'s signature exactly (kernel/fs/fs.h). */
struct sys_list_dir_args {
    const char *path;
    struct fs_dirent *out;
    unsigned int max_entries;
    unsigned int *out_count;
};

/* Removes the entry at path, wrapping fs_delete() (kernel/fs/fs.h) with
 * zero changes to its behavior -- including failing on a non-empty
 * directory. fs_delete() takes a single pointer argument, so unlike the
 * multi-argument syscalls above, arg IS the path pointer directly, not
 * the address of a wrapping struct. */
#define SYS_DELETE 5

/* Creates a new directory at path, wrapping fs_create_dir()
 * (kernel/fs/fs.h) with zero changes to its behavior -- including its
 * write-once semantics. Same single-pointer-argument shape as
 * SYS_DELETE: arg IS the path pointer directly. */
#define SYS_CREATE_DIR 6

/* Renames the entry at path to new_name in place, wrapping fs_rename()
 * (kernel/fs/fs.h) with zero changes to its behavior. arg is the
 * address of a struct sys_rename_args built by the caller. */
#define SYS_RENAME 7

/* Mirrors fs_rename()'s signature exactly (kernel/fs/fs.h). */
struct sys_rename_args {
    const char *path;
    const char *new_name;
};

/* Relocates the file at path into dest_dir, wrapping fs_move()
 * (kernel/fs/fs.h) with zero changes to its behavior -- a metadata-only
 * move, no file data is read or rewritten. arg is the address of a
 * struct sys_move_args built by the caller. */
#define SYS_MOVE 8

/* Mirrors fs_move()'s signature exactly (kernel/fs/fs.h). */
struct sys_move_args {
    const char *path;
    const char *dest_dir;
};

/* Duplicates the file at path into dest_dir, wrapping fs_copy_file()
 * (kernel/fs/fs.h) with zero changes to its behavior -- unlike
 * SYS_MOVE, this allocates and writes a real second copy of the data.
 * arg is the address of a struct sys_copy_file_args built by the
 * caller. */
#define SYS_COPY_FILE 9

/* Same two fields as struct sys_move_args, in the same order -- kept
 * as its own type rather than reused because it mirrors
 * fs_copy_file()'s own signature, a separate function from fs_move(),
 * not because the layout needs to differ. */
struct sys_copy_file_args {
    const char *path;
    const char *dest_dir;
};

/* Appends data to an existing file (or creates it, same as
 * SYS_CREATE_FILE, if it doesn't exist yet), wrapping fs_append_file()
 * (kernel/fs/fs.h) with zero changes to its behavior. arg is the
 * address of a struct sys_append_file_args built by the caller. */
#define SYS_APPEND_FILE 10

/* Mirrors fs_append_file()'s signature exactly (kernel/fs/fs.h); same
 * field shape as struct sys_create_file_args since both functions
 * share that signature. */
struct sys_append_file_args {
    const char *path;
    const void *data;
    unsigned int size;
};

/* Returns gfx_width()/gfx_height() (kernel/gfx/graphics.h) directly --
 * zero arguments, zero changes to behavior. Ignores arg. */
#define SYS_GFX_WIDTH 11
#define SYS_GFX_HEIGHT 12

/* Fills the entire backbuffer with rgb, wrapping gfx_clear() with zero
 * changes to its behavior. A single value fits in one register, so arg
 * IS rgb directly, not the address of a wrapping struct -- same shape
 * as SYS_DELETE/SYS_CREATE_DIR's single-pointer argument, just a plain
 * value instead of a pointer. */
#define SYS_GFX_CLEAR 13

/* Sets one backbuffer pixel, wrapping gfx_put_pixel() with zero changes
 * to its behavior. arg is the address of a struct sys_gfx_put_pixel_args
 * built by the caller. No bounds validation: an out-of-range x/y is
 * whatever gfx_put_pixel() itself already does with one, same
 * no-pointer-validation stance every prior syscall sub-project has
 * taken. */
#define SYS_GFX_PUT_PIXEL 14

/* Mirrors gfx_put_pixel()'s signature exactly (kernel/gfx/graphics.h). */
struct sys_gfx_put_pixel_args {
    int x;
    int y;
    unsigned int rgb;
};

/* Fills a backbuffer rectangle, wrapping gfx_fill_rect() with zero
 * changes to its behavior. arg is the address of a struct
 * sys_gfx_fill_rect_args built by the caller. */
#define SYS_GFX_FILL_RECT 15

/* Mirrors gfx_fill_rect()'s signature exactly (kernel/gfx/graphics.h). */
struct sys_gfx_fill_rect_args {
    int x;
    int y;
    int w;
    int h;
    unsigned int rgb;
};

/* Copies one backbuffer rectangle to the real screen, wrapping
 * gfx_present_rect() with zero changes to its behavior -- this is a
 * ring-3 program's only way to make anything it draws actually
 * visible; the backbuffer itself is never directly readable/writable
 * by ring 3 except through SYS_GFX_PUT_PIXEL/SYS_GFX_FILL_RECT. arg is
 * the address of a struct sys_gfx_present_rect_args built by the
 * caller. */
#define SYS_GFX_PRESENT_RECT 16

/* Mirrors gfx_present_rect()'s signature exactly
 * (kernel/gfx/graphics.h). */
struct sys_gfx_present_rect_args {
    int x;
    int y;
    int w;
    int h;
};

/* Sets voice's waveform, wrapping synth_set_voice_waveform()
 * (kernel/audio/synth.h) with zero changes to its behavior. waveform
 * is a plain int, not an enum synth_waveform -- syscall.h stays
 * dependency-free on synth.h the same way it stays dependency-free on
 * fs.h, so the caller passes the enum's underlying int value and
 * syscall_audio.c casts it back. arg is the address of a struct
 * sys_synth_set_waveform_args built by the caller. */
#define SYS_SYNTH_SET_WAVEFORM 17

/* Mirrors synth_set_voice_waveform()'s signature exactly
 * (kernel/audio/synth.h), with waveform as plain int -- see above. */
struct sys_synth_set_waveform_args {
    int voice;
    int waveform;
};

/* Sets voice's pitch (an index into ona_phase_increment[]), wrapping
 * synth_set_ona() with zero changes to its behavior. arg is the
 * address of a struct sys_synth_set_ona_args built by the caller. */
#define SYS_SYNTH_SET_ONA 18

/* Mirrors synth_set_ona()'s signature exactly (kernel/audio/synth.h). */
struct sys_synth_set_ona_args {
    int voice;
    int ona;
};

/* Sets voice's ADSR envelope, wrapping synth_set_adsr() with zero
 * changes to its behavior. arg is the address of a struct
 * sys_synth_set_adsr_args built by the caller. */
#define SYS_SYNTH_SET_ADSR 19

/* Mirrors synth_set_adsr()'s signature exactly (kernel/audio/synth.h). */
struct sys_synth_set_adsr_args {
    int voice;
    int attack_ms;
    int decay_ms;
    int sustain_percent;
    int release_ms;
};

/* Starts/stops voice's envelope, wrapping synth_gate_on()/
 * synth_gate_off() with zero changes to their behavior. Both take a
 * single plain int argument, so unlike the multi-argument syscalls
 * above, arg IS the voice index directly, not the address of a
 * wrapping struct -- same shape as SYS_DELETE/SYS_GFX_CLEAR's
 * single-value argument. */
#define SYS_SYNTH_GATE_ON 20
#define SYS_SYNTH_GATE_OFF 21

/* Opens the sole ring-3-owned window slot (WIN_KIND_RING3,
 * kernel/kernel.c), wrapping the new window_ring3_open() with zero
 * ambiguity about what it does -- there's no pre-existing subsystem
 * header for window management the way fs.h/graphics.h/synth.h exist
 * for the other syscall families, since kernel.c's window system has
 * no reusable API of its own yet, only inline logic. Draws and
 * presents the window's chrome synchronously, inside the syscall
 * itself -- kmain()'s own frame loop never runs again once
 * enter_ring3() has been called, so nothing else ever would. No
 * dragging, no click/keyboard event delivery to the ring-3 program --
 * see docs/superpowers/specs/2026-08-29-ring3-window-design.md. arg is
 * the address of a struct sys_window_open_args built by the caller. */
#define SYS_WINDOW_OPEN 22

/* Mirrors window_ring3_open()'s signature exactly (kernel/kernel.c). */
struct sys_window_open_args {
    int x;
    int y;
    int w;
    int h;
    const char *title;
};

/* Marks the ring-3 window closed, wrapping the new window_ring3_close().
 * Does NOT erase its already-presented pixels -- see
 * window_ring3_close()'s own comment (kernel/kernel.c) and the design
 * spec's "Known limitation" section. Ignores arg. */
#define SYS_WINDOW_CLOSE 23

/* Blocks until a click or close event lands on the ring-3 window,
 * wrapping the new ring3_wait_event() (kernel/kernel.c). arg is the
 * address of a struct sys_wait_event_args the caller builds; its
 * fields are filled in by the syscall, not read from by it. See
 * docs/superpowers/specs/2026-08-29-ring3-window-events-design.md. */
#define SYS_WAIT_EVENT 24

#define RING3_EVENT_NONE 0
#define RING3_EVENT_CLICK 1
#define RING3_EVENT_CLOSED 2
#define RING3_EVENT_KEY 3

/* Mirrors ring3_wait_event()'s four output parameters (kernel/kernel.c).
 * type is one of the RING3_EVENT_* values above; x/y are valid only
 * when type == RING3_EVENT_CLICK, and are absolute screen coordinates
 * -- matching every other syscall a ring-3 program already uses
 * (SYS_WINDOW_OPEN's x/y, every gfx syscall), not a new window-relative
 * convention. key is valid only when type == RING3_EVENT_KEY: the same
 * char keyboard_poll_char() (kernel/drivers/keyboard.h) already
 * produces -- printable ASCII, '\b', '\n', or one of the KEY_UP/DOWN/
 * LEFT/RIGHT/HOME/END/DELETE pseudo-codes. No raw scancodes or
 * modifier keys; ring-3 programs get the same reduced vocabulary every
 * other focused text field in this kernel already receives. See
 * docs/superpowers/specs/2026-08-29-ring3-window-keyboard-events-design.md. */
struct sys_wait_event_args {
    int type;
    int x;
    int y;
    char key;
};

/* Pure -- exactly what sub-project (B) shipped as syscall_dispatch(),
 * renamed. SYS_TEST/SYS_EXIT/default only, zero dependency on fs.h,
 * graphics.h, synth.h, kernel.c's window state, or any other kernel
 * module. This is what kernel/tests/test_syscall.c links and calls
 * directly. */
int syscall_dispatch_core(int num, int arg);

/* Real, window-backed -- defined in syscall_window.c, not syscall.c,
 * syscall_fs.c, syscall_gfx.c, or syscall_audio.c. Handles
 * SYS_WINDOW_OPEN/SYS_WINDOW_CLOSE/SYS_WAIT_EVENT, falls through to
 * syscall_dispatch_core() for everything else. Not the function
 * ring3.asm calls directly -- syscall_dispatch_audio() (below) calls
 * this one as its own fallback, extending the dispatcher chain to five
 * links. SYS_WAIT_EVENT is the first syscall in this codebase that can
 * genuinely block for an unbounded time -- see
 * docs/superpowers/specs/2026-08-29-ring3-window-events-design.md for
 * why that's safe (it re-drives kmain_frame() itself while blocked,
 * rather than the desktop freezing). Dragging/raising/closing the
 * ring-3 window already work the same as any other window kind; only
 * content clicks and closes are reported as events. */
int syscall_dispatch_window(int num, int arg);

/* Real, audio-backed -- defined in syscall_audio.c, not syscall.c,
 * syscall_fs.c, or syscall_gfx.c. Handles the synth.h-backed syscalls
 * (SYS_SYNTH_SET_WAVEFORM, SYS_SYNTH_SET_ONA, SYS_SYNTH_SET_ADSR,
 * SYS_SYNTH_GATE_ON, SYS_SYNTH_GATE_OFF), falls through to
 * syscall_dispatch_window() (not syscall_dispatch_core() directly
 * anymore) for everything else. Not the function ring3.asm calls
 * directly -- syscall_dispatch_gfx() (below) calls this one as its own
 * fallback, extending the same dispatcher chain gfx already added onto
 * fs. Only a starter set of synth.h's much larger API (duty cycle,
 * ring modulation, filter routing, arpeggio, and the global filter
 * cutoff/resonance/mode are all still unwrapped) -- enough to make a
 * voice play a note from ring 3, not full coverage, the same YAGNI
 * scoping the gfx slice used. */
int syscall_dispatch_audio(int num, int arg);

/* Real, gfx-backed -- defined in syscall_gfx.c, not syscall.c or
 * syscall_fs.c. Handles the graphics.h-backed syscalls (SYS_GFX_WIDTH,
 * SYS_GFX_HEIGHT, SYS_GFX_CLEAR, SYS_GFX_PUT_PIXEL, SYS_GFX_FILL_RECT,
 * SYS_GFX_PRESENT_RECT), falls through to syscall_dispatch_audio() (not
 * syscall_dispatch_core() directly anymore) for everything else. Not
 * the function ring3.asm calls directly -- syscall_dispatch() (below)
 * calls this one as its own fallback, chaining fs.h's dispatcher,
 * graphics.h's dispatcher, and synth.h's dispatcher together.
 * Deliberately no per-window clipping or ownership: a ring-3 program
 * can draw anywhere in the shared backbuffer, including over the
 * desktop/taskbar -- real ownership is out of scope for this slice,
 * deferred to whenever window-management syscalls are tackled. */
int syscall_dispatch_gfx(int num, int arg);

/* Real -- defined in syscall_fs.c, not syscall.c. This is the exact
 * name ring3.asm's syscall_entry already calls; giving the real
 * dispatcher this name in a different file means ring3.asm needs no
 * changes at all. Handles the fs.h-backed syscalls (SYS_READ_FILE,
 * SYS_CREATE_FILE, SYS_LIST_DIR, SYS_DELETE, SYS_CREATE_DIR,
 * SYS_RENAME, SYS_MOVE, SYS_COPY_FILE, SYS_APPEND_FILE) -- covering
 * fs.h's entire operation surface -- falls through to
 * syscall_dispatch_gfx() (not syscall_dispatch_core() directly
 * anymore) for everything else. eax carries the syscall number (num)
 * and ebx the argument (arg) across int 0x80; the return value here is
 * what eax holds when it returns (see ring3.asm's syscall_entry). */
int syscall_dispatch(int num, int arg);

#endif
