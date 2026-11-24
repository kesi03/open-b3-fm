/* setBfree - DSP tonewheel organ
 *
 * Copyright (C) 2013-2018 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * Freestanding B3 firmware adaptation: the locale-guard macros and the
 * program/state includes were removed; the engine only needs synth+midicfg.
 */
#ifndef GLOBAL_INST_H
#define GLOBAL_INST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "midi.h"
#include "tonegen.h"

struct b_reverb;
struct b_whirl;
struct b_programme;
struct b_preamp;

typedef struct b_instance {
    struct b_reverb *reverb;
    struct b_whirl *whirl;
    struct b_tonegen *synth;
    struct b_programme *progs;
    void *midicfg;
    void *preamp;
    void *state;
} b_instance;

#ifdef __cplusplus
}
#endif
#endif
