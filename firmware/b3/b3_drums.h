/* B3 firmware - Hammond-style rhythm voices and eight 16-step grooves.
 * Derived from the Apache-2.0 drum engine in FUNKY-TRAIN (keste), adapted for
 * jazz organ comping: ride cymbal, brush, rim, soft kick and cross-stick.
 * SPDX-License-Identifier: AGPL-3.0-or-later (this firmware)
 */
#ifndef B3_DRUMS_H
#define B3_DRUMS_H

#include "vibe.h"
#include "vibe_dsp.h"

#define B3_DRUM_VOICES 12
#define B3_DRUM_TRACKS 8
#define B3_DRUM_STEPS 16
#define B3_RHYTHMS 8

enum {
  B3_D_KICK = 0, B3_D_SNARE, B3_D_RIM, B3_D_HAT,
  B3_D_OPEN, B3_D_RIDE, B3_D_TOM, B3_D_BRUSH,
  B3_D_COUNT
};

typedef struct {
  float gain, tune, decay, drive, tone, pan; /* pan: -1 left .. +1 right */
} b3_drum_ctrl;

void b3_drums_init(float sr);
void b3_drums_reset(void);
void b3_drums_trigger(int voice, int vel);              /* audio flow */
void b3_drums_render(float *l, float *r, int n, const b3_drum_ctrl *c); /* audio flow */
unsigned char b3_drum_step(int pat, int step);
const char *b3_rhythm_name(int pat);
const char *b3_drum_voice_name(int voice);

#endif
