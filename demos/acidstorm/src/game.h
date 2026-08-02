#ifndef GAME_H
#define GAME_H

#include <stdbool.h>

typedef struct {
    float move_x, move_y;   /* desired movement dir, magnitude 0..1 */
    float aim_x, aim_y;     /* desired aim/fire dir, magnitude 0..1 (0,0 = not firing) */
    bool  confirm;          /* edge-triggered: start / advance */
    bool  pause;            /* edge-triggered: toggle pause */
    bool  restart;          /* edge-triggered: restart after game over */
} GameInput;

void game_init(void);
void game_update(double dt, const GameInput *in);
void game_render(void);
void game_get_player_pos(float *x, float *y);

#endif
