#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include "gfx.h"
#include "game.h"

static float apply_deadzone(float v, float dz) {
    if (fabsf(v) < dz) return 0.0f;
    float s = v > 0 ? 1.0f : -1.0f;
    return s * ((fabsf(v) - dz) / (1.0f - dz));
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "ACIDSTORM",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        FB_W * 3, FB_H * 3,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_RenderSetLogicalSize(renderer, FB_W, FB_H);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);

    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);

    SDL_GameController *pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    }

    game_init();

    Uint64 prev_ticks = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    bool running = true;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            else if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                pad = SDL_GameControllerOpen(ev.cdevice.which);
            } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED && pad) {
                if (SDL_GameControllerGetJoystick(pad) &&
                    SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == ev.cdevice.which) {
                    SDL_GameControllerClose(pad);
                    pad = NULL;
                }
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);

        float move_x = 0, move_y = 0;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) move_x -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) move_x += 1.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) move_y -= 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) move_y += 1.0f;

        float aim_x = 0, aim_y = 0;
        if (keys[SDL_SCANCODE_KP_4]) aim_x -= 1.0f;
        if (keys[SDL_SCANCODE_KP_6]) aim_x += 1.0f;
        if (keys[SDL_SCANCODE_KP_8]) aim_y -= 1.0f;
        if (keys[SDL_SCANCODE_KP_2]) aim_y += 1.0f;

        if (pad) {
            float lx = apply_deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f, 0.22f);
            float ly = apply_deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f, 0.22f);
            if (fabsf(lx) > 0.001f || fabsf(ly) > 0.001f) { move_x = lx; move_y = ly; }

            float rx = apply_deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f, 0.28f);
            float ry = apply_deadzone(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f, 0.28f);
            if (fabsf(rx) > 0.001f || fabsf(ry) > 0.001f) { aim_x = rx; aim_y = ry; }
        }

        /* mouse aim: only when left button held and no keyboard/pad aim given */
        if (aim_x == 0.0f && aim_y == 0.0f) {
            int mwx, mwy;
            Uint32 mstate = SDL_GetMouseState(&mwx, &mwy);
            if (mstate & SDL_BUTTON(SDL_BUTTON_LEFT)) {
                int win_w, win_h;
                SDL_GetWindowSize(window, &win_w, &win_h);
                float scale = win_w / (float)FB_W < win_h / (float)FB_H ?
                    win_w / (float)FB_W : win_h / (float)FB_H;
                if (scale < 1.0f) scale = 1.0f;
                float sw = FB_W * scale, sh = FB_H * scale;
                float offx = (win_w - sw) * 0.5f, offy = (win_h - sh) * 0.5f;
                float fbx = (mwx - offx) / scale, fby = (mwy - offy) / scale;
                float px, py;
                game_get_player_pos(&px, &py);
                float dx = fbx - px, dy = fby - py;
                float d = sqrtf(dx * dx + dy * dy);
                if (d > 4.0f) { aim_x = dx / d; aim_y = dy / d; }
            }
        }

        GameInput in = {0};
        in.move_x = move_x; in.move_y = move_y;
        in.aim_x = aim_x; in.aim_y = aim_y;
        in.confirm = keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_RETURN] ||
                     (pad && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) ||
                     (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT));
        in.pause = keys[SDL_SCANCODE_P] || (pad && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START));
        in.restart = keys[SDL_SCANCODE_R];

        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - prev_ticks) / (double)freq;
        prev_ticks = now;
        if (dt > 0.05) dt = 0.05; /* clamp for hitches */

        game_update(dt, &in);
        game_render();

        SDL_UpdateTexture(tex, NULL, g_fb, FB_W * (int)sizeof(Color));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, tex, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
