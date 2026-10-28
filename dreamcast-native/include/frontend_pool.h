#ifndef DAYTONA_NATIVE_FRONTEND_POOL_H
#define DAYTONA_NATIVE_FRONTEND_POOL_H
#include "collision.h"
#include "visibility.h"

/* The part of the original's car pool (504e00 + id*0x300, 44 entities) the race entry inherits from the scene
 * before it, and what goes with it (include/frontend.h has the rules that fill and apply it). */
typedef struct {
    nm_collision_travel_t motion[44]; /* +28c/+290 previous, +294/+298 travel */
    uint32_t relative[44];            /* +2a4 */
    uint8_t side[44];                 /* +27d */
    int16_t rank_key[44];             /* +60 (the grid reset e3f4 keeps it; the race start rewrites it) */
    uint16_t rank_order[44], track_order[44]; /* 1224c's lists (the race start rebuilds them, the demo start does not) */
    int8_t leading_lap;               /* 5014ba */
    /* The visible cells behind the bitmap 501520 as the last scene's scenery object 5890 left them:
     * the race's first detail pass reads them (nm_frontend_race_guided). */
    nm_scenery_cells_t cells;
} nm_entity_pool_t;

#endif
