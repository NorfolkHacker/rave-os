#include <SDL.h>

extern "C" {
#include "../core/hal/hal_keycode.h"
}

#define KEY_QUEUE_CAP 16

static int s_key_queue[ KEY_QUEUE_CAP ];
static int s_queue_head = 0;
static int s_queue_tail = 0;
static Uint8 s_prev_state[ SDL_NUM_SCANCODES ];

static void
queue_push( int code )
{
    int next = ( s_queue_tail + 1 ) % KEY_QUEUE_CAP;
    if( next == s_queue_head )
    {
        return; /* full: drop this transition, same best-effort tradeoff as
                  * every other high-frequency input path in this project
                  * (kernel_router.c's non-blocking send_event) */
    }
    s_key_queue[ s_queue_tail ] = code;
    s_queue_tail = next;
}

static bool
queue_pop( int * code )
{
    if( s_queue_head == s_queue_tail )
    {
        return false;
    }
    *code = s_key_queue[ s_queue_head ];
    s_queue_head = ( s_queue_head + 1 ) % KEY_QUEUE_CAP;
    return true;
}

/* Translates one SDL scancode (a physical key position) to this project's
 * keycode vocabulary (hal_keycode.h), resolving Shift at translate time so
 * every caller downstream (kernel_router, Ruby apps) only ever sees a
 * resolved character or a named KERNEL_KEY_* constant, never a raw
 * scancode or a modifier bit to interpret itself. Returns 0 for anything
 * unmapped (silently ignored -- e.g. function keys, Ctrl/Alt, which this
 * first pass doesn't support, see the phase 5 keyboard design spec). */
static int
translate_scancode( SDL_Scancode sc, bool shift )
{
    if( sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z )
    {
        int letter = 'a' + ( sc - SDL_SCANCODE_A );
        return shift ? ( letter - 32 ) : letter;
    }

    if( sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9 )
    {
        static const char unshifted[] = "123456789";
        static const char shifted[]   = "!@#$%^&*(";
        int i = sc - SDL_SCANCODE_1;
        return shift ? shifted[ i ] : unshifted[ i ];
    }
    if( sc == SDL_SCANCODE_0 )
    {
        return shift ? ')' : '0';
    }

    switch( sc )
    {
        case SDL_SCANCODE_SPACE:        return ' ';
        case SDL_SCANCODE_RETURN:       return KERNEL_KEY_ENTER;
        case SDL_SCANCODE_KP_ENTER:     return KERNEL_KEY_ENTER;
        case SDL_SCANCODE_BACKSPACE:    return KERNEL_KEY_BACKSPACE;
        case SDL_SCANCODE_ESCAPE:       return KERNEL_KEY_ESCAPE;
        case SDL_SCANCODE_TAB:          return KERNEL_KEY_TAB;
        case SDL_SCANCODE_DELETE:       return KERNEL_KEY_DELETE;
        case SDL_SCANCODE_UP:           return KERNEL_KEY_UP;
        case SDL_SCANCODE_DOWN:         return KERNEL_KEY_DOWN;
        case SDL_SCANCODE_LEFT:         return KERNEL_KEY_LEFT;
        case SDL_SCANCODE_RIGHT:        return KERNEL_KEY_RIGHT;
        case SDL_SCANCODE_MINUS:        return shift ? '_' : '-';
        case SDL_SCANCODE_EQUALS:       return shift ? '+' : '=';
        case SDL_SCANCODE_LEFTBRACKET:  return shift ? '{' : '[';
        case SDL_SCANCODE_RIGHTBRACKET: return shift ? '}' : ']';
        case SDL_SCANCODE_BACKSLASH:    return shift ? '|' : '\\';
        case SDL_SCANCODE_SEMICOLON:    return shift ? ':' : ';';
        case SDL_SCANCODE_APOSTROPHE:   return shift ? '"' : '\'';
        case SDL_SCANCODE_COMMA:        return shift ? '<' : ',';
        case SDL_SCANCODE_PERIOD:       return shift ? '>' : '.';
        case SDL_SCANCODE_SLASH:        return shift ? '?' : '/';
        case SDL_SCANCODE_GRAVE:        return shift ? '~' : '`';
        default:                        return 0;
    }
}

/* SDL_GetKeyboardState's backing array is kept current by Panel_sdl's own
 * SDL_PollEvent loop, which runs continuously on the main thread
 * (Panel_sdl::loop -> _event_proc). Reading it here, from the FreeRTOS
 * thread, is the same category of cross-thread read hal_input_poll_touch's
 * sim implementation already relies on for monitor.touch_x/y/touched (set
 * by that same main-thread event loop) -- no new synchronization is
 * introduced here because none was needed there either. */
static void
key_scan( void )
{
    int numkeys = 0;
    const Uint8 * state = SDL_GetKeyboardState( &numkeys );
    bool shift = state[ SDL_SCANCODE_LSHIFT ] || state[ SDL_SCANCODE_RSHIFT ];

    int limit = numkeys < SDL_NUM_SCANCODES ? numkeys : SDL_NUM_SCANCODES;
    for( int i = 0; i < limit; i++ )
    {
        bool down = state[ i ] != 0;
        bool was_down = s_prev_state[ i ] != 0;
        if( down && !was_down )
        {
            int code = translate_scancode( ( SDL_Scancode ) i, shift );
            if( code != 0 )
            {
                queue_push( code );
            }
        }
        s_prev_state[ i ] = state[ i ];
    }
}

extern "C" void
hal_input_poll_key( int * keycode, bool * pressed )
{
    key_scan();
    int code;
    if( queue_pop( &code ) )
    {
        *keycode = code;
        *pressed = true;
    }
    else
    {
        *pressed = false;
    }
}
