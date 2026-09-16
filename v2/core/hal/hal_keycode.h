#ifndef ACID_HAL_KEYCODE_H
#define ACID_HAL_KEYCODE_H

/* hal_input_poll_key's keycode vocabulary. Printable keys report their
 * plain ASCII value (32-126) directly -- correct case/symbol already
 * resolved against the Shift state at the point the HAL detects the press,
 * so app code never handles modifier state itself, matching this project's
 * "translate at the HAL boundary" convention (hal_input_poll_touch already
 * hands back resolved coordinates, not raw device units). Non-printable
 * keys use one of the named constants below, chosen from a range (256+)
 * that can never collide with a printable ASCII value.
 *
 * These values must stay numerically in sync with v2/apps/lib/acid_keys.rb
 * -- there is no shared header between C and mruby in this project (see
 * vm_host.c's own comment on why: no require/require_relative gem), so the
 * two are hand-kept in agreement. Checked in phase 5's plan self-review;
 * check again if either file changes later. */
#define KERNEL_KEY_ENTER     257
#define KERNEL_KEY_BACKSPACE 258
#define KERNEL_KEY_ESCAPE    259
#define KERNEL_KEY_TAB       260
#define KERNEL_KEY_DELETE    261
#define KERNEL_KEY_UP        262
#define KERNEL_KEY_DOWN      263
#define KERNEL_KEY_LEFT      264
#define KERNEL_KEY_RIGHT     265

#endif
