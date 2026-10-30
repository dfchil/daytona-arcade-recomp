/* NATIVE_PAD_MONKEY (local.cfg.mk): a scripted pad on port A in place of the real one, for an unattended console
 * run through several games on the production paths (attract, credit, select, race, results, name entry, attract
 * again). Linked with --wrap=enj_ctrl_get_states and --wrap=enj_ctrl_map_states; enDjinn and the product's sources
 * are unchanged.
 *
 * The script steps once a frame, in enDjinn's own pad refresh (enj_ctrl_map_states: the first thing of a frame in
 * enj_state_run_frame, before the mode's updater). The game reads the pad from several places in one frame (the
 * updater, the render's preparation), and a step keyed on a vblank or flip counter could change between two of
 * them, since those advance in an interrupt; this one cannot. The engine's own getter call, before its first
 * frame, is served too (the array is static). Button edges are those of enDjinn's two-bit states (enj_ctrl.h:
 * DOWN_THIS_FRAME on the step a button goes down, UP_THIS_FRAME on the step it goes up, one frame each), as a
 * real pad's. The run's end prints the step count against the PVR's flips, as the check of the once-a-frame key. */
#include <enDjinn/enj_enDjinn.h>
#include <dc/pvr.h>
#include <stdio.h>
#include <string.h>

#ifndef NATIVE_PAD_MONKEY_FRAMES
#define NATIVE_PAD_MONKEY_FRAMES 60000
#endif
#define MONKEY_FRAMES ((unsigned)(NATIVE_PAD_MONKEY_FRAMES))

/* The schedule, in frames. */
enum {
    MONKEY_GAS_PERIOD=330,   /* the accelerator's cycle: held at 255, then released for MONKEY_GAS_RELEASE frames
                                (the select screens confirm on the trigger's edge: under 96, then 160 or more) */
    MONKEY_GAS_RELEASE=40,
    MONKEY_STICK_PERIOD=720, /* one swing from the left lock to the right and back (a triangle wave) */
    MONKEY_START_HOLD=4,     /* frames START stays down at a press */
    MONKEY_START_GAP=600,    /* frames from one START press to the next, plus the drift */
    MONKEY_START_DRIFT=43,   /* the drift grows by this at every press, modulo MONKEY_START_SPAN: the presses walk
                                through the attract loop (cinematic, ranking, title, V.R. demo) */
    MONKEY_START_SPAN=200,
};

enj_ctrlr_state_t **__real_enj_ctrl_get_states(void);
size_t __real_enj_ctrl_map_states(void);

static enj_ctrlr_state_t monkey_state;                   /* port A's state the game reads */
static enj_ctrlr_state_t *monkey_refs[MAPLE_PORT_COUNT]; /* the array handed out: port A scripted, the rest real */
static unsigned monkey_frame, monkey_presses, monkey_next_start=MONKEY_START_GAP, monkey_done;
static unsigned monkey_calls, monkey_calls_max, monkey_frames_without_call;
static unsigned long monkey_calls_total;
static char monkey_mode[ENJ_MODE_NAME_CAPACITY];

/* enDjinn's table (enj_ctrl.c enj_update_button_state): bit 0 the level, bit 1 a change on this step. */
static uint32_t monkey_button(uint32_t previous, unsigned down) {
    const uint32_t level=down ? 1u : 0u;
    return level|(((previous&1u)!=level) ? 2u : 0u);
}

static const char *monkey_mode_name(void) {
    const enj_mode_t *mode=enj_mode_get();
    return mode ? mode->name : "(no mode)";
}

static void monkey_step(void) {
    const unsigned frame=monkey_frame++;
    if(frame) {
        if(monkey_calls>monkey_calls_max) monkey_calls_max=monkey_calls;
        if(!monkey_calls) ++monkey_frames_without_call;
    }
    monkey_calls=0;
    const char *mode=monkey_mode_name();
    if(strcmp(mode,monkey_mode)) {
        snprintf(monkey_mode,sizeof(monkey_mode),"%s",mode);
        printf("Pad monkey: frame %u, mode %s\n",frame,mode);
    }
    unsigned start=0;
    int8_t joyx=0;
    uint8_t rtrigger=0;
    if(frame<MONKEY_FRAMES) {
        rtrigger=frame%MONKEY_GAS_PERIOD<MONKEY_GAS_RELEASE ? 0u : 255u;
        const unsigned half=MONKEY_STICK_PERIOD/2u,phase=frame%MONKEY_STICK_PERIOD;
        const unsigned ramp=(phase<half ? phase : MONKEY_STICK_PERIOD-phase)*254u/half; /* 0..254 */
        joyx=(int8_t)((int)ramp-127);
        if(frame>=monkey_next_start) {
            if(frame<monkey_next_start+MONKEY_START_HOLD) {
                start=1;
                if(frame==monkey_next_start)
                    printf("Pad monkey: START press %u at frame %u in %s\n",monkey_presses+1u,frame,mode);
            } else {
                monkey_next_start+=MONKEY_START_GAP+(monkey_presses*MONKEY_START_DRIFT)%MONKEY_START_SPAN;
                ++monkey_presses;
            }
        }
    }
    monkey_state.button.START=monkey_button(monkey_state.button.START,start);
    monkey_state.joyx=joyx;monkey_state.joyy=0;
    monkey_state.ltrigger=0;monkey_state.rtrigger=rtrigger; /* the left trigger never: the settings chord */
    monkey_state.portnum=0;monkey_state.state=NULL;
    if(frame>=MONKEY_FRAMES && !monkey_done) {
        monkey_done=1;
        pvr_stats_t stats;
        const int have_stats=pvr_get_stats(&stats)==0;
        printf("Pad monkey: done at frame %u in %s: %u START presses, %lu pad reads (at most %u a frame, %u frames without one), "
            "PVR flips %lu vblanks %lu; pad released, returning to loader\n",frame,mode,monkey_presses,monkey_calls_total,
            monkey_calls_max,monkey_frames_without_call,have_stats ? (unsigned long)stats.frame_count : 0ul,
            have_stats ? (unsigned long)stats.vbl_count : 0ul);
        enj_state_flag_shutdown(NULL);
    }
}

size_t __wrap_enj_ctrl_map_states(void) {
    const size_t mapped=__real_enj_ctrl_map_states();
    monkey_step();
    return mapped;
}

enj_ctrlr_state_t **__wrap_enj_ctrl_get_states(void) {
    enj_ctrlr_state_t **real=__real_enj_ctrl_get_states();
    for(size_t i=1;i<MAPLE_PORT_COUNT;++i) monkey_refs[i]=real[i];
    monkey_refs[0]=&monkey_state;
    ++monkey_calls;++monkey_calls_total;
    return monkey_refs;
}
