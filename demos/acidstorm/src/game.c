#include "game.h"
#include "gfx.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ---- arena layout ---- */
#define ARENA_X0 4
#define ARENA_Y0 20
#define ARENA_X1 (FB_W - 4)
#define ARENA_Y1 (FB_H - 4)

#define MAX_ENTITIES 640
#define MAX_HUMANS 8
#define MAX_ELECTRODES 12

typedef enum {
    EK_NONE = 0, EK_PBULLET, EK_EBULLET, EK_GRUNT, EK_ENFORCER,
    EK_HULK, EK_HUMAN, EK_ELECTRODE, EK_PARTICLE
} EntKind;

typedef struct {
    EntKind kind;
    bool active;
    float x, y, vx, vy;
    float radius;
    int hp;
    float timer;
    float phase;
    Color color;
} Entity;

typedef enum { ST_TITLE, ST_PLAYING, ST_WAVE_CLEAR, ST_GAME_OVER } GState;

static Entity g_ent[MAX_ENTITIES];
static Entity g_player;
static GState g_state;
static bool g_paused;
static long g_score;
static int g_lives;
static int g_wave;
static double g_time;
static double g_state_timer;
static float g_fire_cd;
static float g_invuln;
static float g_facing_x = 0.0f, g_facing_y = -1.0f;
static int g_humans_alive;
static int g_wave_bonus_shown;
static bool g_prev_confirm, g_prev_pause, g_prev_restart;

static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static float frange(float a, float b) { return a + frand() * (b - a); }

static Entity *spawn(EntKind k, float x, float y) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        if (!g_ent[i].active) {
            Entity *e = &g_ent[i];
            memset(e, 0, sizeof(*e));
            e->kind = k;
            e->active = true;
            e->x = x; e->y = y;
            return e;
        }
    }
    return NULL;
}

static void spawn_burst(float x, float y, Color c, int n) {
    for (int i = 0; i < n; i++) {
        Entity *p = spawn(EK_PARTICLE, x, y);
        if (!p) return;
        float a = frange(0.0f, 6.2832f);
        float sp = frange(20.0f, 90.0f);
        p->vx = cosf(a) * sp;
        p->vy = sinf(a) * sp;
        p->timer = frange(0.25f, 0.6f);
        p->radius = p->timer;
        p->color = c;
    }
}

static bool circle_hit(const Entity *a, const Entity *b) {
    float dx = a->x - b->x, dy = a->y - b->y;
    float r = a->radius + b->radius;
    return dx * dx + dy * dy <= r * r;
}

static void clamp_arena(float *x, float *y, float r) {
    if (*x < ARENA_X0 + r) *x = ARENA_X0 + r;
    if (*x > ARENA_X1 - r) *x = ARENA_X1 - r;
    if (*y < ARENA_Y0 + r) *y = ARENA_Y0 + r;
    if (*y > ARENA_Y1 - r) *y = ARENA_Y1 - r;
}

static void clear_entities(void) {
    for (int i = 0; i < MAX_ENTITIES; i++) g_ent[i].active = false;
}

static void player_hit(void) {
    if (g_invuln > 0.0f) return;
    spawn_burst(g_player.x, g_player.y, RGB(255, 60, 60), 24);
    g_lives--;
    if (g_lives <= 0) {
        g_state = ST_GAME_OVER;
        g_state_timer = 0.0;
    } else {
        g_player.x = (ARENA_X0 + ARENA_X1) * 0.5f;
        g_player.y = (ARENA_Y0 + ARENA_Y1) * 0.5f;
        g_invuln = 2.0f;
    }
}

static void wave_start(int wave) {
    clear_entities();
    g_wave = wave;
    g_player.x = (ARENA_X0 + ARENA_X1) * 0.5f;
    g_player.y = (ARENA_Y0 + ARENA_Y1) * 0.5f;
    g_invuln = 1.0f;

    int humans = 4 + (wave % 3);
    if (humans > MAX_HUMANS) humans = MAX_HUMANS;
    g_humans_alive = humans;
    for (int i = 0; i < humans; i++) {
        Entity *h = spawn(EK_HUMAN, frange(ARENA_X0 + 10, ARENA_X1 - 10), frange(ARENA_Y0 + 10, ARENA_Y1 - 10));
        if (!h) break;
        h->radius = 3.0f;
        h->timer = frange(0.5f, 2.0f);
        float a = frange(0, 6.2832f);
        h->vx = cosf(a) * 14.0f; h->vy = sinf(a) * 14.0f;
    }

    int electrodes = 2 + wave / 2;
    if (electrodes > MAX_ELECTRODES) electrodes = MAX_ELECTRODES;
    for (int i = 0; i < electrodes; i++) {
        float x, y;
        do {
            x = frange(ARENA_X0 + 12, ARENA_X1 - 12);
            y = frange(ARENA_Y0 + 12, ARENA_Y1 - 12);
        } while (fabsf(x - g_player.x) < 24 && fabsf(y - g_player.y) < 24);
        Entity *e = spawn(EK_ELECTRODE, x, y);
        if (!e) break;
        e->radius = 4.0f;
    }

    int grunts = 4 + wave * 2;
    if (grunts > 26) grunts = 26;
    for (int i = 0; i < grunts; i++) {
        float x, y;
        do {
            x = frange(ARENA_X0 + 4, ARENA_X1 - 4);
            y = frange(ARENA_Y0 + 4, ARENA_Y1 - 4);
        } while (fabsf(x - g_player.x) < 40 && fabsf(y - g_player.y) < 40);
        Entity *g = spawn(EK_GRUNT, x, y);
        if (!g) break;
        g->radius = 3.5f;
        g->hp = 1;
    }

    if (wave >= 2) {
        int enforcers = 1 + (wave - 2) / 2;
        if (enforcers > 8) enforcers = 8;
        for (int i = 0; i < enforcers; i++) {
            Entity *e = spawn(EK_ENFORCER, frange(ARENA_X0 + 10, ARENA_X1 - 10), frange(ARENA_Y0 + 10, ARENA_Y1 - 10));
            if (!e) break;
            e->radius = 3.5f;
            e->hp = 1;
            e->timer = frange(0.5f, 1.5f);
            e->phase = frange(0, 6.2832f);
        }
    }

    if (wave >= 3) {
        int hulks = 1 + (wave - 3) / 3;
        if (hulks > 5) hulks = 5;
        for (int i = 0; i < hulks; i++) {
            Entity *h = spawn(EK_HULK, frange(ARENA_X0 + 10, ARENA_X1 - 10), frange(ARENA_Y0 + 10, ARENA_Y1 - 10));
            if (!h) break;
            h->radius = 5.0f;
            h->hp = 3;
        }
    }
}

static void start_new_game(void) {
    g_score = 0;
    g_lives = 3;
    g_paused = false;
    wave_start(1);
    g_state = ST_PLAYING;
}

void game_get_player_pos(float *x, float *y) {
    *x = g_player.x;
    *y = g_player.y;
}

void game_init(void) {
    srand(42u ^ (unsigned)(size_t)&g_ent);
    g_state = ST_TITLE;
    g_state_timer = 0.0;
    g_time = 0.0;
    memset(&g_player, 0, sizeof(g_player));
    g_player.radius = 3.5f;
    clear_entities();
}

static int count_hostiles(void) {
    int n = 0;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &g_ent[i];
        if (e->active && (e->kind == EK_GRUNT || e->kind == EK_ENFORCER || e->kind == EK_HULK)) n++;
    }
    return n;
}

static void update_playing(double dt, const GameInput *in) {
    float fdt = (float)dt;

    if (g_invuln > 0.0f) g_invuln -= fdt;

    /* movement */
    float mlen = sqrtf(in->move_x * in->move_x + in->move_y * in->move_y);
    float mx = in->move_x, my = in->move_y;
    if (mlen > 1.0f) { mx /= mlen; my /= mlen; }
    const float PLAYER_SPEED = 78.0f;
    g_player.x += mx * PLAYER_SPEED * fdt;
    g_player.y += my * PLAYER_SPEED * fdt;
    clamp_arena(&g_player.x, &g_player.y, g_player.radius);

    /* aiming / firing */
    float alen = sqrtf(in->aim_x * in->aim_x + in->aim_y * in->aim_y);
    if (alen > 0.35f) {
        g_facing_x = in->aim_x / alen;
        g_facing_y = in->aim_y / alen;
        g_fire_cd -= fdt;
        if (g_fire_cd <= 0.0f) {
            Entity *b = spawn(EK_PBULLET, g_player.x + g_facing_x * (g_player.radius + 2.0f),
                                            g_player.y + g_facing_y * (g_player.radius + 2.0f));
            if (b) {
                const float BS = 230.0f;
                b->vx = g_facing_x * BS;
                b->vy = g_facing_y * BS;
                b->radius = 1.5f;
                b->timer = 1.5f;
            }
            g_fire_cd = 0.11f;
        }
    } else if (mlen > 0.2f) {
        g_facing_x = mx; g_facing_y = my;
    }

    /* entity updates */
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &g_ent[i];
        if (!e->active) continue;
        switch (e->kind) {
        case EK_PBULLET:
            e->x += e->vx * fdt; e->y += e->vy * fdt;
            e->timer -= fdt;
            if (e->timer <= 0 || e->x < ARENA_X0 || e->x > ARENA_X1 || e->y < ARENA_Y0 || e->y > ARENA_Y1)
                e->active = false;
            break;
        case EK_EBULLET:
            e->x += e->vx * fdt; e->y += e->vy * fdt;
            e->timer -= fdt;
            if (e->timer <= 0 || e->x < ARENA_X0 || e->x > ARENA_X1 || e->y < ARENA_Y0 || e->y > ARENA_Y1) {
                e->active = false; break;
            }
            if (circle_hit(e, &g_player)) { e->active = false; player_hit(); }
            break;
        case EK_GRUNT: {
            float dx = g_player.x - e->x, dy = g_player.y - e->y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 0.001f) {
                float sp = 32.0f + g_wave * 1.2f;
                if (sp > 60.0f) sp = 60.0f;
                e->x += dx / d * sp * fdt;
                e->y += dy / d * sp * fdt;
            }
            if (circle_hit(e, &g_player)) player_hit();
            break;
        }
        case EK_ENFORCER: {
            e->phase += fdt * 3.0f;
            float dx = g_player.x - e->x, dy = g_player.y - e->y;
            float d = sqrtf(dx * dx + dy * dy);
            float wobble_x = cosf(e->phase) * 40.0f;
            float wobble_y = sinf(e->phase * 1.3f) * 40.0f;
            float tx = g_player.x + wobble_x, ty = g_player.y + wobble_y;
            float ddx = tx - e->x, ddy = ty - e->y;
            float dd = sqrtf(ddx * ddx + ddy * ddy);
            if (dd > 0.001f) { e->x += ddx / dd * 26.0f * fdt; e->y += ddy / dd * 26.0f * fdt; }
            clamp_arena(&e->x, &e->y, e->radius);
            e->timer -= fdt;
            if (e->timer <= 0.0f && d > 0.001f) {
                Entity *b = spawn(EK_EBULLET, e->x, e->y);
                if (b) {
                    const float BS = 90.0f;
                    b->vx = dx / d * BS; b->vy = dy / d * BS;
                    b->radius = 1.5f; b->timer = 3.0f;
                }
                e->timer = frange(1.0f, 1.8f);
            }
            if (circle_hit(e, &g_player)) player_hit();
            break;
        }
        case EK_HULK: {
            Entity *target = NULL;
            float best = 1e9f;
            for (int j = 0; j < MAX_ENTITIES; j++) {
                Entity *h = &g_ent[j];
                if (h->active && h->kind == EK_HUMAN) {
                    float dx = h->x - e->x, dy = h->y - e->y;
                    float d = dx * dx + dy * dy;
                    if (d < best) { best = d; target = h; }
                }
            }
            float tx = target ? target->x : g_player.x;
            float ty = target ? target->y : g_player.y;
            float dx = tx - e->x, dy = ty - e->y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 0.001f) { e->x += dx / d * 16.0f * fdt; e->y += dy / d * 16.0f * fdt; }
            if (target && circle_hit(e, target)) {
                target->active = false;
                g_humans_alive--;
                spawn_burst(target->x, target->y, RGB(255, 255, 255), 10);
            }
            if (circle_hit(e, &g_player)) player_hit();
            break;
        }
        case EK_HUMAN:
            e->timer -= fdt;
            if (e->timer <= 0.0f) {
                float a = frange(0, 6.2832f);
                e->vx = cosf(a) * 14.0f; e->vy = sinf(a) * 14.0f;
                e->timer = frange(0.6f, 2.2f);
            }
            e->x += e->vx * fdt; e->y += e->vy * fdt;
            clamp_arena(&e->x, &e->y, e->radius);
            if (circle_hit(&g_player, e)) {
                e->active = false;
                g_humans_alive--;
                g_score += 1000;
                spawn_burst(e->x, e->y, RGB(255, 255, 120), 14);
            }
            break;
        case EK_ELECTRODE:
            if (circle_hit(e, &g_player)) player_hit();
            break;
        case EK_PARTICLE:
            e->x += e->vx * fdt; e->y += e->vy * fdt;
            e->vx *= 0.92f; e->vy *= 0.92f;
            e->timer -= fdt;
            if (e->timer <= 0.0f) e->active = false;
            break;
        default: break;
        }
    }

    /* bullet vs enemy collisions */
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *b = &g_ent[i];
        if (!b->active || b->kind != EK_PBULLET) continue;
        for (int j = 0; j < MAX_ENTITIES; j++) {
            Entity *e = &g_ent[j];
            if (!e->active) continue;
            if (e->kind != EK_GRUNT && e->kind != EK_ENFORCER && e->kind != EK_HULK) continue;
            if (!circle_hit(b, e)) continue;
            b->active = false;
            e->hp--;
            if (e->hp <= 0) {
                e->active = false;
                long pts = e->kind == EK_GRUNT ? 100 : e->kind == EK_ENFORCER ? 150 : 500;
                g_score += pts;
                spawn_burst(e->x, e->y, e->kind == EK_HULK ? RGB(255, 160, 40) : RGB(80, 255, 220), 16);
            } else {
                spawn_burst(b->x, b->y, RGB(255, 255, 255), 4);
            }
            break;
        }
    }

    if (count_hostiles() == 0) {
        long bonus = 100L * g_wave + 50L * g_humans_alive;
        g_score += bonus;
        g_wave_bonus_shown = (int)bonus;
        g_state = ST_WAVE_CLEAR;
        g_state_timer = 0.0;
    }
}

void game_update(double dt, const GameInput *in) {
    g_time += dt;
    bool confirm_edge = in->confirm && !g_prev_confirm;
    bool pause_edge = in->pause && !g_prev_pause;
    bool restart_edge = in->restart && !g_prev_restart;
    g_prev_confirm = in->confirm;
    g_prev_pause = in->pause;
    g_prev_restart = in->restart;

    switch (g_state) {
    case ST_TITLE:
        if (confirm_edge) start_new_game();
        break;
    case ST_PLAYING:
        if (pause_edge) g_paused = !g_paused;
        if (!g_paused) update_playing(dt, in);
        break;
    case ST_WAVE_CLEAR:
        g_state_timer += dt;
        if (g_state_timer > 2.2) {
            wave_start(g_wave + 1);
            g_state = ST_PLAYING;
        }
        break;
    case ST_GAME_OVER:
        g_state_timer += dt;
        if ((confirm_edge || restart_edge) && g_state_timer > 0.4) {
            g_state = ST_TITLE;
        }
        break;
    }
}

/* ---------------- rendering ---------------- */

static void draw_hud(void) {
    gfx_fill_rect(0, 0, FB_W, ARENA_Y0 - 2, RGB(10, 4, 20));
    char buf[64];
    snprintf(buf, sizeof buf, "SCORE %06ld", g_score);
    gfx_text(4, 4, buf, RGB(0, 255, 220), 1);
    snprintf(buf, sizeof buf, "WAVE %02d", g_wave);
    gfx_text(FB_W / 2 - 20, 4, buf, RGB(255, 220, 0), 1);
    snprintf(buf, sizeof buf, "LIVES %d", g_lives < 0 ? 0 : g_lives);
    gfx_text(FB_W - 74, 4, buf, RGB(255, 60, 200), 1);
}

static void draw_arena_border(void) {
    Color c = hsv_to_rgb(fmod(g_time * 0.15, 1.0), 0.9, 1.0);
    gfx_rect(ARENA_X0 - 2, ARENA_Y0 - 2, ARENA_X1 - ARENA_X0 + 4, ARENA_Y1 - ARENA_Y0 + 4, c);
}

static void draw_entities(void) {
    for (int i = 0; i < MAX_ENTITIES; i++) {
        Entity *e = &g_ent[i];
        if (!e->active) continue;
        switch (e->kind) {
        case EK_PBULLET:
            gfx_fill_rect((int)e->x - 1, (int)e->y - 1, 2, 2, RGB(255, 255, 255));
            break;
        case EK_EBULLET:
            gfx_fill_rect((int)e->x - 1, (int)e->y - 1, 2, 2, RGB(255, 90, 40));
            break;
        case EK_GRUNT: {
            Color c = hsv_to_rgb(fmod(g_time * 0.6 + i * 0.05, 1.0), 1.0, 1.0);
            gfx_fill_rect((int)(e->x - e->radius), (int)(e->y - e->radius), (int)(e->radius * 2), (int)(e->radius * 2), c);
            break;
        }
        case EK_ENFORCER:
            gfx_fill_circle((int)e->x, (int)e->y, (int)e->radius, RGB(255, 230, 0));
            gfx_circle((int)e->x, (int)e->y, (int)e->radius + 1, RGB(255, 120, 220));
            break;
        case EK_HULK:
            gfx_fill_rect((int)(e->x - e->radius), (int)(e->y - e->radius), (int)(e->radius * 2), (int)(e->radius * 2), RGB(255, 140, 30));
            gfx_rect((int)(e->x - e->radius) - 1, (int)(e->y - e->radius) - 1, (int)(e->radius * 2) + 2, (int)(e->radius * 2) + 2, RGB(120, 40, 0));
            break;
        case EK_HUMAN:
            gfx_fill_circle((int)e->x, (int)e->y, 2, RGB(180, 255, 180));
            break;
        case EK_ELECTRODE: {
            Color c = ((int)(g_time * 6.0) % 2) ? RGB(255, 255, 0) : RGB(200, 0, 255);
            gfx_fill_circle((int)e->x, (int)e->y, 3, c);
            gfx_line((int)e->x - 4, (int)e->y, (int)e->x + 4, (int)e->y, c);
            gfx_line((int)e->x, (int)e->y - 4, (int)e->x, (int)e->y + 4, c);
            break;
        }
        case EK_PARTICLE: {
            int r = (int)(e->radius);
            if (r < 1) r = 1;
            gfx_fill_rect((int)e->x - r / 2, (int)e->y - r / 2, r, r, e->color);
            break;
        }
        default: break;
        }
    }
}

static void draw_player(void) {
    if (g_invuln > 0.0f && ((int)(g_invuln * 12.0f) % 2 == 0)) return;
    gfx_fill_circle((int)g_player.x, (int)g_player.y, (int)g_player.radius, RGB(0, 255, 180));
    gfx_circle((int)g_player.x, (int)g_player.y, (int)g_player.radius + 1, RGB(255, 255, 255));
    float bx = g_player.x + g_facing_x * (g_player.radius + 3.0f);
    float by = g_player.y + g_facing_y * (g_player.radius + 3.0f);
    gfx_fill_rect((int)bx - 1, (int)by - 1, 2, 2, RGB(255, 255, 255));
}

void game_render(void) {
    gfx_plasma(g_state == ST_TITLE ? g_time : g_time * 0.4);

    if (g_state == ST_TITLE) {
        const char *title = "ACIDSTORM";
        int w = gfx_text_width(title, 3);
        gfx_text((FB_W - w) / 2, 60, title, hsv_to_rgb(fmod(g_time * 0.5, 1.0), 1.0, 1.0), 3);
        const char *sub = "AN ACID ARENA SHOOTER";
        gfx_text((FB_W - gfx_text_width(sub, 1)) / 2, 100, sub, RGB(255, 255, 255), 1);
        if (((int)(g_time * 2.0)) % 2 == 0) {
            const char *msg = "PRESS SPACE TO START";
            gfx_text((FB_W - gfx_text_width(msg, 1)) / 2, 160, msg, RGB(0, 255, 255), 1);
        }
        const char *ctrl = "WASD MOVE   ARROWS-MOUSE-PAD AIM/FIRE";
        gfx_text((FB_W - gfx_text_width(ctrl, 1)) / 2, 200, ctrl, RGB(200, 200, 255), 1);
        return;
    }

    /* darken plasma for gameplay so entities read clearly */
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) {
            uint32_t c = g_fb[y][x];
            uint32_t r = ((c >> 16) & 0xFF) / 4, gg = ((c >> 8) & 0xFF) / 4, b = (c & 0xFF) / 4;
            g_fb[y][x] = RGB(r, gg, b);
        }

    draw_arena_border();
    draw_entities();
    draw_player();
    draw_hud();

    if (g_state == ST_WAVE_CLEAR) {
        char buf[48];
        snprintf(buf, sizeof buf, "WAVE %02d CLEAR", g_wave);
        gfx_text((FB_W - gfx_text_width(buf, 2)) / 2, 100, buf, RGB(0, 255, 180), 2);
        snprintf(buf, sizeof buf, "BONUS %d", g_wave_bonus_shown);
        gfx_text((FB_W - gfx_text_width(buf, 1)) / 2, 124, buf, RGB(255, 230, 0), 1);
    } else if (g_state == ST_GAME_OVER) {
        const char *msg = "GAME OVER";
        gfx_text((FB_W - gfx_text_width(msg, 2)) / 2, 90, msg, RGB(255, 40, 90), 2);
        char buf[48];
        snprintf(buf, sizeof buf, "FINAL SCORE %06ld", g_score);
        gfx_text((FB_W - gfx_text_width(buf, 1)) / 2, 116, buf, RGB(255, 255, 255), 1);
        if (((int)(g_time * 2.0)) % 2 == 0) {
            const char *msg2 = "PRESS SPACE";
            gfx_text((FB_W - gfx_text_width(msg2, 1)) / 2, 140, msg2, RGB(0, 255, 255), 1);
        }
    } else if (g_paused) {
        const char *msg = "PAUSED";
        gfx_text((FB_W - gfx_text_width(msg, 2)) / 2, 110, msg, RGB(255, 255, 255), 2);
    }
}
