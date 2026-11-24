/* B3 firmware - VIBE FM front end for the OpenB3/setBfree Beatrix engine.
 *
 * The audio engine in this folder is the actual Beatrix C engine (tonegen,
 * vibrato, reverb, whirl, overdrive/eqcomp).  This file supplies the VIBE FM
 * lifecycle, parameters, notes, pads, rhythm sequencer, display and FOCUS.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * See LICENSE and NOTICE.txt for the engine's GPL-2.0-or-later notices.
 */
#include "vibe.h"
#include "vibe_dsp.h"
#include "vibe_gfx.h"

#include <string.h>

#include "b3_compat.h"
#include "global_definitions.h"
#include "global_inst.h"
#include "tonegen.h"
#include "vibrato.h"
#include "reverb.h"
#include "whirl.h"
#include "overdrive.h"
#include "b3_drums.h"

VIBE_PARALLEL_AUDIO;

/* ------------------------------------------------------------------ layout */
#define PAGE_SOUND 0
#define PAGE_LOWER 1
#define PAGE_VIB   2
#define PAGE_FX    3
#define PAGE_RHY   4
#define PAGE_SEQ   5
#define PAGE_COUNT 6

#define COL_BG      0x100a18
#define COL_PANEL   0x1e1230
#define COL_PANEL2  0x301e4b
#define COL_LINE    0x5b3a80
#define COL_PURPLE  0xb45cff
#define COL_LILAC   0xe6c6ff
#define COL_CREAM   0xf5ebff
#define COL_MUTED   0xba9fd1
#define COL_DIM     0x8b72a6
#define COL_GREEN   0x9bcb63
#define COL_RED     0xe06a8f
#define COL_BLUE    0x8fa8ff
#define COL_FMIX    0xff9ad5

/* ----------------------------------------------------------------- params */
enum {
  P_PRESET = 0,
  P_SWELL, P_MASTER,
  P_U1, P_U2, P_U3, P_U4, P_U5, P_U6, P_U7, P_U8, P_U9,
  P_L1, P_L2, P_L3, P_L4, P_L5, P_L6, P_L7, P_L8, P_L9,
  P_P1, P_P2, P_P3, P_P4, P_P5, P_P6, P_P7, P_P8, P_P9,
  P_VIB, P_VIB_UPPER, P_VIB_LOWER,
  P_PERC_ON, P_PERC_FAST, P_PERC_SOFT, P_PERC_FIRST,
  P_CLEAN, P_DRIVE, P_OUTPUT,
  P_REVERB, P_ROTARY,
  P_RHYTHM, P_RUN, P_BPM, P_SWING, P_DRUM_LEVEL, P_STAB_LEN, P_CHORD,

  /* SDK 11 mixer, appended so every existing automation index stays put. */
  P_MIX1_LEVEL, P_MIX1_PAN, P_MIX1_MUTE, P_MIX1_SOLO, P_MIX1_ARM,
  P_MIX2_LEVEL, P_MIX2_PAN, P_MIX2_MUTE, P_MIX2_SOLO, P_MIX2_ARM,
  P_MIX3_LEVEL, P_MIX3_PAN, P_MIX3_MUTE, P_MIX3_SOLO, P_MIX3_ARM,
  P_MIX4_LEVEL, P_MIX4_PAN, P_MIX4_MUTE, P_MIX4_SOLO, P_MIX4_ARM,
  P_MIX_SELECT,
  P_COUNT
};

#define MIX_SLOTS 4
#define MIX_LEVEL(i) (P_MIX1_LEVEL + (i) * 5)
#define MIX_PAN(i)   (MIX_LEVEL(i) + 1)
#define MIX_MUTE(i)  (MIX_LEVEL(i) + 2)
#define MIX_SOLO(i)  (MIX_LEVEL(i) + 3)
#define MIX_ARM(i)   (MIX_LEVEL(i) + 4)
#define MIX_LAST     P_MIX_SELECT

static const char *const CH_OFF_ON[]     = { "OFF", "ON" };
static const char *const CH_VIB[]        = { "OFF", "VIB1", "CHO1", "VIB2", "CHO2", "VIB3", "CHO3" };
static const char *const CH_ROTARY[]     = { "STOP", "SLOW", "FAST" };
static const char *const CH_RHYTHM[]     = {
  "JAZZ RIDE", "SHUFFLE", "BOSSA", "FUNK", "GOSPEL", "ROCK", "LATIN", "BALLAD"
};
static const char *const CH_CHORD[]      = {
  "MAJ", "MIN", "7TH", "M7", "MAJ7", "SUS4", "DIM", "6/9"
};
static const char *const CH_MIX_SLOT[]   = {
  "ORGAN 1", "ORGAN 2", "ORGAN 3", "DRUMS"
};

/* ---------------------------------------------------------------- presets */
/* Named B3 setups: drawbars, scanner, percussion, preamp, Leslie and groove.
 * Preset 0 is also the power-on voice, so the engine plays immediately. */
static const char *const PRESET_NAMES[] = {
  "JAZZ", "JIMMY SMITH", "GOSPEL", "GREEN ONIONS",
  "SOUL", "FUNK", "LATIN", "GEORGIE FAME",
  "BOOKER T", "MOTOWN", "BLUES", "BALLAD",
  "SHUFFLE", "BOSSA NOVA", "REGGAE", "DISCO",
  "ROCK", "DIXIE", "WORSHIP", "FLUTE",
  "BRASS", "STRINGS", "PERCUSSION", "OVERDRIVE"
};
#define NPRESET (int)(sizeof(PRESET_NAMES) / sizeof(PRESET_NAMES[0]))

typedef struct {
  const char *name;
  unsigned char u[9], l[9], p[9];       /* upper / lower / pedal drawbars */
  unsigned char vib, vib_u, vib_l;
  unsigned char perc, perc_fast, perc_soft, perc_first;
  unsigned char clean;
  float drive, out, reverb, swell, master;
  unsigned char rotary;
  unsigned char rhythm;
  float bpm, swing, drum_level;
  unsigned char chord;
} b3_preset;

/* Drawbar order: 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'. */
static const b3_preset PRESETS[NPRESET] = {
  { "JAZZ", .u={8,8,8,8,0,0,0,0,0}, .l={8,8,8,4,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=2,.vib_u=1,.vib_l=0,.perc=0,.clean=1,.drive=0.10f,.out=0.85f,.reverb=0.10f,
    .swell=1.0f,.master=0.92f,.rotary=1,.rhythm=0,.bpm=108,.swing=0.34f,.drum_level=0.42f,.chord=2 },
  { "JIMMY SMITH", .u={8,8,8,0,0,0,0,0,0}, .l={8,8,8,8,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=0,.vib_u=0,.vib_l=0,.perc=1,.perc_fast=1,.perc_soft=0,.perc_first=1,
    .clean=0,.drive=0.42f,.out=0.90f,.reverb=0.14f,.swell=1.0f,.master=0.42f,.rotary=2,
    .rhythm=0,.bpm=118,.swing=0.28f,.drum_level=0.38f,.chord=2 },
  { "GOSPEL", .u={8,8,8,8,8,0,0,0,0}, .l={8,8,8,8,0,0,0,0,8}, .p={8,8,0,0,0,0,0,0,0},
    .vib=3,.vib_u=1,.vib_l=0,.perc=0,.clean=1,.drive=0.20f,.out=0.95f,.reverb=0.22f,
    .swell=1.15f,.master=0.74f,.rotary=2,.rhythm=4,.bpm=96,.swing=0.18f,.drum_level=0.55f,.chord=4 },
  { "GREEN ONIONS", .u={8,8,8,8,0,0,0,0,0}, .l={8,8,8,4,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=0,.vib_u=0,.vib_l=0,.perc=1,.perc_fast=0,.perc_soft=1,.perc_first=0,
    .clean=0,.drive=0.30f,.out=0.90f,.reverb=0.08f,.swell=1.0f,.master=0.42f,.rotary=1,
    .rhythm=3,.bpm=112,.swing=0.16f,.drum_level=0.50f,.chord=0 },
  { "SOUL", .u={8,8,8,6,0,0,0,0,0}, .l={8,8,8,8,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=4,.vib_u=1,.vib_l=1,.perc=0,.clean=1,.drive=0.16f,.out=0.88f,.reverb=0.16f,
    .swell=1.05f,.master=0.80f,.rotary=1,.rhythm=5,.bpm=104,.swing=0.24f,.drum_level=0.50f,.chord=2 },
  { "FUNK", .u={8,8,6,6,0,0,0,0,0}, .l={8,8,8,0,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=0,.vib_u=0,.vib_l=0,.perc=1,.perc_fast=1,.perc_soft=0,.perc_first=1,
    .clean=0,.drive=0.34f,.out=0.86f,.reverb=0.08f,.swell=1.0f,.master=0.50f,.rotary=1,
    .rhythm=3,.bpm=110,.swing=0.30f,.drum_level=0.62f,.chord=6 },
  { "LATIN", .u={8,8,8,8,5,0,0,0,0}, .l={8,8,8,4,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=5,.vib_u=1,.vib_l=0,.perc=1,.perc_fast=0,.perc_soft=1,.perc_first=0,
    .clean=1,.drive=0.12f,.out=0.88f,.reverb=0.18f,.swell=1.0f,.master=0.82f,.rotary=2,
    .rhythm=6,.bpm=126,.swing=0.10f,.drum_level=0.58f,.chord=5 },
  { "GEORGIE FAME", .u={8,8,8,8,8,0,0,0,0}, .l={8,8,8,8,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=2,.vib_u=1,.vib_l=0,.perc=1,.perc_fast=0,.perc_soft=1,.perc_first=0,
    .clean=1,.drive=0.22f,.out=0.90f,.reverb=0.12f,.swell=1.0f,.master=0.72f,.rotary=2,
    .rhythm=1,.bpm=120,.swing=0.26f,.drum_level=0.48f,.chord=7 },
  { "BOOKER T", .u={8,8,8,6,0,0,0,0,0}, .l={8,8,8,4,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=0,.vib_u=0,.vib_l=0,.perc=1,.perc_fast=0,.perc_soft=0,.perc_first=1,
    .clean=0,.drive=0.28f,.out=0.92f,.reverb=0.06f,.swell=1.0f,.master=0.44f,.rotary=1,
    .rhythm=3,.bpm=108,.swing=0.20f,.drum_level=0.46f,.chord=2 },
  { "MOTOWN", .u={8,8,8,4,0,0,0,0,0}, .l={8,8,8,8,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=3,.vib_u=1,.vib_l=1,.perc=0,.clean=1,.drive=0.14f,.out=0.86f,.reverb=0.10f,
    .swell=1.0f,.master=0.80f,.rotary=2,.rhythm=3,.bpm=116,.swing=0.22f,.drum_level=0.60f,.chord=0 },
  { "BLUES", .u={8,8,8,8,0,0,0,0,0}, .l={8,8,8,0,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=1,.vib_u=1,.vib_l=0,.perc=1,.perc_fast=0,.perc_soft=0,.perc_first=1,
    .clean=0,.drive=0.38f,.out=0.88f,.reverb=0.12f,.swell=1.0f,.master=0.48f,.rotary=1,
    .rhythm=1,.bpm=94,.swing=0.42f,.drum_level=0.42f,.chord=2 },
  { "BALLAD", .u={8,0,8,6,0,0,4,0,2}, .l={8,8,8,4,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=5,.vib_u=1,.vib_l=1,.perc=0,.clean=1,.drive=0.06f,.out=0.84f,.reverb=0.30f,
    .swell=1.05f,.master=0.86f,.rotary=1,.rhythm=7,.bpm=76,.swing=0.08f,.drum_level=0.36f,.chord=4 },
  { "SHUFFLE", .u={8,8,8,6,0,0,0,0,0}, .l={8,8,8,8,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=4,.vib_u=1,.vib_l=0,.perc=0,.clean=1,.drive=0.18f,.out=0.86f,.reverb=0.14f,
    .swell=1.0f,.master=0.78f,.rotary=2,.rhythm=1,.bpm=122,.swing=0.58f,.drum_level=0.52f,.chord=7 },
  { "BOSSA NOVA", .u={8,0,8,6,0,3,0,0,0}, .l={8,8,8,0,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=6,.vib_u=1,.vib_l=1,.perc=0,.clean=1,.drive=0.08f,.out=0.82f,.reverb=0.20f,
    .swell=1.0f,.master=0.88f,.rotary=1,.rhythm=2,.bpm=132,.swing=0.06f,.drum_level=0.50f,.chord=4 },
  { "REGGAE", .u={8,8,8,0,0,0,0,0,0}, .l={8,8,8,4,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=2,.vib_u=1,.vib_l=0,.perc=1,.perc_fast=1,.perc_soft=1,.perc_first=0,
    .clean=1,.drive=0.20f,.out=0.86f,.reverb=0.24f,.swell=1.0f,.master=0.76f,.rotary=2,
    .rhythm=6,.bpm=84,.swing=0.12f,.drum_level=0.54f,.chord=3 },
  { "DISCO", .u={8,8,8,8,6,0,0,0,0}, .l={8,8,8,8,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=4,.vib_u=1,.vib_l=0,.perc=0,.clean=0,.drive=0.24f,.out=0.90f,.reverb=0.14f,
    .swell=1.1f,.master=0.44f,.rotary=2,.rhythm=5,.bpm=128,.swing=0.04f,.drum_level=0.66f,.chord=7 },
  { "ROCK", .u={8,8,8,8,8,0,0,0,0}, .l={8,8,8,8,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=0,.vib_u=0,.vib_l=0,.perc=1,.perc_fast=1,.perc_soft=0,.perc_first=1,
    .clean=0,.drive=0.55f,.out=0.92f,.reverb=0.08f,.swell=1.0f,.master=0.46f,.rotary=2,
    .rhythm=5,.bpm=132,.swing=0.02f,.drum_level=0.72f,.chord=0 },
  { "DIXIE", .u={0,0,8,8,0,0,0,0,0}, .l={8,8,8,0,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=1,.vib_u=1,.vib_l=1,.perc=1,.perc_fast=1,.perc_soft=1,.perc_first=1,
    .clean=1,.drive=0.10f,.out=0.84f,.reverb=0.16f,.swell=1.0f,.master=0.95f,.rotary=1,
    .rhythm=1,.bpm=118,.swing=0.48f,.drum_level=0.44f,.chord=0 },
  { "WORSHIP", .u={8,8,8,8,8,8,0,0,0}, .l={8,8,8,8,8,0,0,0,8}, .p={8,8,0,0,0,0,0,0,0},
    .vib=2,.vib_u=1,.vib_l=1,.perc=0,.clean=1,.drive=0.12f,.out=0.94f,.reverb=0.34f,
    .swell=1.20f,.master=0.72f,.rotary=2,.rhythm=4,.bpm=88,.swing=0.10f,.drum_level=0.48f,.chord=4 },
  { "FLUTE", .u={0,0,8,8,0,0,0,0,0}, .l={0,0,8,4,0,0,0,0,0}, .p={8,0,0,0,0,0,0,0,0},
    .vib=3,.vib_u=1,.vib_l=1,.perc=0,.clean=1,.drive=0.0f,.out=0.82f,.reverb=0.28f,
    .swell=1.0f,.master=0.95f,.rotary=1,.rhythm=7,.bpm=82,.swing=0.08f,.drum_level=0.34f,.chord=4 },
  { "BRASS", .u={8,8,8,8,0,8,0,0,0}, .l={8,8,8,8,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=0,.vib_u=0,.vib_l=0,.perc=1,.perc_fast=1,.perc_soft=0,.perc_first=1,
    .clean=0,.drive=0.32f,.out=0.88f,.reverb=0.10f,.swell=1.05f,.master=0.46f,.rotary=1,
    .rhythm=6,.bpm=120,.swing=0.14f,.drum_level=0.52f,.chord=0 },
  { "STRINGS", .u={8,0,8,8,4,0,4,0,2}, .l={8,8,8,4,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=6,.vib_u=1,.vib_l=1,.perc=0,.clean=1,.drive=0.04f,.out=0.84f,.reverb=0.36f,
    .swell=1.1f,.master=1.00f,.rotary=1,.rhythm=7,.bpm=80,.swing=0.06f,.drum_level=0.32f,.chord=4 },
  { "PERCUSSION", .u={0,0,0,8,8,0,8,0,0}, .l={8,8,8,0,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=0,.vib_u=0,.vib_l=0,.perc=1,.perc_fast=1,.perc_soft=0,.perc_first=1,
    .clean=1,.drive=0.16f,.out=0.86f,.reverb=0.18f,.swell=1.35f,.master=0.95f,.rotary=1,
    .rhythm=6,.bpm=124,.swing=0.20f,.drum_level=0.60f,.chord=6 },
  { "OVERDRIVE", .u={8,8,8,8,0,0,0,0,0}, .l={8,8,8,8,0,0,0,0,8}, .p={8,0,0,0,0,0,0,0,0},
    .vib=0,.vib_u=0,.vib_l=0,.perc=1,.perc_fast=1,.perc_soft=0,.perc_first=1,
    .clean=0,.drive=0.72f,.out=0.94f,.reverb=0.10f,.swell=1.15f,.master=0.36f,.rotary=2,
    .rhythm=5,.bpm=138,.swing=0.04f,.drum_level=0.70f,.chord=0 }
};

typedef char preset_names_must_match[
  (sizeof(PRESETS) / sizeof(PRESETS[0]) == NPRESET) ? 1 : -1];

#define PA(id, name, lo, hi, def, unit, steps, choices) { id, name, lo, hi, def, unit, steps, choices }
static const vibe_param PARAMS[P_COUNT] = {
  PA("preset", "Preset", 0.f, (float)(NPRESET - 1), 0.f, "", NPRESET, PRESET_NAMES),
  PA("swell", "Swell", 0.f, 1.5f, 1.f, "", 0, 0),
  PA("master", "Master", 0.f, 2.f, 1.f, "", 0, 0),

  PA("upper_16", "U 16'", 0.f, 8.f, 8.f, "", 0, 0),
  PA("upper_513", "U 5 1/3'", 0.f, 8.f, 8.f, "", 0, 0),
  PA("upper_8", "U 8'", 0.f, 8.f, 6.f, "", 0, 0),
  PA("upper_4", "U 4'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("upper_223", "U 2 2/3'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("upper_2", "U 2'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("upper_135", "U 1 3/5'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("upper_113", "U 1 1/3'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("upper_1", "U 1'", 0.f, 8.f, 0.f, "", 0, 0),

  PA("lower_16", "L 16'", 0.f, 8.f, 8.f, "", 0, 0),
  PA("lower_513", "L 5 1/3'", 0.f, 8.f, 8.f, "", 0, 0),
  PA("lower_8", "L 8'", 0.f, 8.f, 8.f, "", 0, 0),
  PA("lower_4", "L 4'", 0.f, 8.f, 8.f, "", 0, 0),
  PA("lower_223", "L 2 2/3'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("lower_2", "L 2'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("lower_135", "L 1 3/5'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("lower_113", "L 1 1/3'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("lower_1", "L 1'", 0.f, 8.f, 8.f, "", 0, 0),

  PA("pedal_32", "P 32'", 0.f, 8.f, 8.f, "", 0, 0),
  PA("pedal_16", "P 16'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("pedal_8", "P 8'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("pedal_4", "P 4'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("pedal_223", "P 2 2/3'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("pedal_2", "P 2'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("pedal_135", "P 1 3/5'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("pedal_113", "P 1 1/3'", 0.f, 8.f, 0.f, "", 0, 0),
  PA("pedal_1", "P 1'", 0.f, 8.f, 0.f, "", 0, 0),

  PA("vibrato", "Vibrato", 0.f, 6.f, 0.f, "", 7, CH_VIB),
  PA("vibrato_upper", "Vib upper", 0.f, 1.f, 1.f, "", 2, CH_OFF_ON),
  PA("vibrato_lower", "Vib lower", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("percussion", "Percussion", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("perc_fast", "Perc fast", 0.f, 1.f, 1.f, "", 2, CH_OFF_ON),
  PA("perc_soft", "Perc soft", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("perc_first", "Perc first", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),

  PA("clean", "Clean preamp", 0.f, 1.f, 1.f, "", 2, CH_OFF_ON),
  PA("drive", "Drive", 0.f, 1.f, 0.35f, "", 0, 0),
  PA("output_gain", "Preamp out", 0.f, 1.f, 0.85f, "", 0, 0),
  PA("reverb", "Reverb", 0.f, 1.f, 0.12f, "", 0, 0),
  PA("rotary", "Rotary", 0.f, 2.f, 1.f, "", 3, CH_ROTARY),

  PA("rhythm", "Rhythm", 0.f, 7.f, 0.f, "", 8, CH_RHYTHM),
  PA("rhythm_run", "Rhythm run", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("bpm", "Tempo", 60.f, 200.f, 112.f, "bpm", 0, 0),
  PA("swing", "Swing", 0.f, 0.75f, 0.12f, "", 0, 0),
  PA("drum_level", "Drum level", 0.f, 1.f, 0.55f, "", 0, 0),
  PA("stab_len", "Stab length", 0.08f, 1.5f, 0.42f, "s", 0, 0),
  PA("chord", "Chord", 0.f, 7.f, 0.f, "", 8, CH_CHORD),

  PA("mix_1_level", "O1 LEVEL", 0.f, 1.f, 1.f, "", 0, 0),
  PA("mix_1_pan", "O1 PAN", -1.f, 1.f, 0.f, "", 0, 0),
  PA("mix_1_mute", "O1 MUTE", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_1_solo", "O1 SOLO", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_1_arm", "O1 ARM", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_2_level", "O2 LEVEL", 0.f, 1.f, 1.f, "", 0, 0),
  PA("mix_2_pan", "O2 PAN", -1.f, 1.f, 0.f, "", 0, 0),
  PA("mix_2_mute", "O2 MUTE", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_2_solo", "O2 SOLO", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_2_arm", "O2 ARM", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_3_level", "O3 LEVEL", 0.f, 1.f, 1.f, "", 0, 0),
  PA("mix_3_pan", "O3 PAN", -1.f, 1.f, 0.f, "", 0, 0),
  PA("mix_3_mute", "O3 MUTE", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_3_solo", "O3 SOLO", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_3_arm", "O3 ARM", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_4_level", "DRUM LEVEL", 0.f, 1.f, 0.55f, "", 0, 0),
  PA("mix_4_pan", "DRUM PAN", -1.f, 1.f, 0.f, "", 0, 0),
  PA("mix_4_mute", "DRUM MUTE", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_4_solo", "DRUM SOLO", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_4_arm", "DRUM ARM", 0.f, 1.f, 0.f, "", 2, CH_OFF_ON),
  PA("mix_select", "Slot", 1.f, 4.f, 1.f, "", MIX_SLOTS, CH_MIX_SLOT)
};
#undef PA

static const char *const GROUPS[P_COUNT] = {
  "A · PRESET",
  "A · TONE", "A · TONE",
  "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS",
  "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS",
  "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS",
  "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS",
  "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS",
  "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS", "B · DRAWBARS",
  "C · VIB PERC", "C · VIB PERC", "C · VIB PERC", "C · VIB PERC", "C · VIB PERC",
  "C · VIB PERC", "C · VIB PERC",
  "D · FX", "D · FX", "D · FX", "D · FX", "D · FX",
  "E · RHYTHM", "E · RHYTHM", "E · RHYTHM", "E · RHYTHM", "E · RHYTHM",
  "E · RHYTHM", "E · RHYTHM",
  "F · MIX LEVELS", "F · MIX LEVELS", "F · MIX SWITCHES", "F · MIX SWITCHES",
  "F · MIX SWITCHES",
  "F · MIX LEVELS", "F · MIX LEVELS", "F · MIX SWITCHES", "F · MIX SWITCHES",
  "F · MIX SWITCHES",
  "F · MIX LEVELS", "F · MIX LEVELS", "F · MIX SWITCHES", "F · MIX SWITCHES",
  "F · MIX SWITCHES",
  "F · MIX LEVELS", "F · MIX LEVELS", "F · MIX SWITCHES", "F · MIX SWITCHES",
  "F · MIX SWITCHES",
  "F · MIX SELECT"
};

typedef char groups_must_match[
  (sizeof(GROUPS) / sizeof(GROUPS[0]) == P_COUNT) ? 1 : -1];

static const vibe_card CARDS[] = {
  { "PRESET", 1, "24 classic B3 setups, from jazz to overdrive" },
  { "TONE", 4, "Swell and overall output level" },
  { "DRAWBARS", 6, "Upper, lower and pedal drawbars (0 - 8)" },
  { "VIB PERC", 4, "Scanner vibrato, chorus and percussion" },
  { "FX", 4, "Preamp, reverb and rotary speaker" },
  { "RHYTHM", 6, "Pads 1-8 start grooves; pads 9-16 play chord stabs" },
  { "MIX LEVELS", 8, "Four slot faders and pans: upper, lower, pedal, drums" },
  { "MIX SWITCHES", 12, "Mute, solo and arm for each of the four slots" },
  { "MIX SELECT", 1, "Choose the slot edited by a mixer controller" }
};

static const signed char CHORD_INTERVALS[8][5] = {
  { 0, 4, 7, -1, -1 },   /* major */
  { 0, 3, 7, -1, -1 },   /* minor */
  { 0, 4, 7, 10, -1 },   /* dominant 7 */
  { 0, 3, 7, 10, -1 },   /* minor 7 */
  { 0, 4, 7, 11, -1 },   /* major 7 */
  { 0, 5, 7, -1, -1 },   /* sus4 */
  { 0, 3, 6, -1, -1 },   /* diminished */
  { 0, 4, 7, 9, 14 }     /* 6/9 */
};

/* --------------------------------------------------------------- control side */
static float g_p[P_COUNT];
static float g_focus_val[P_COUNT + 2];
static unsigned char g_db[3][9];
static short g_held[16][128];
static int g_root_note = 60;
static int g_page = PAGE_SOUND;
static int g_help = 0;
static int g_last_chord = -1;
static int g_shift = 0;
static int g_applying_preset = 0;
static int g_mirror_drum = 0;
static unsigned int g_draw_tick = 0;

static void apply_preset(int idx);

/* ------------------------------------------------------------ command ring */
enum {
  CMD_NOTE_ON = 1, CMD_NOTE_OFF, CMD_DRAWBAR, CMD_PARAM,
  CMD_PANIC, CMD_RHYTHM, CMD_RUN, CMD_STAB
};

typedef struct {
  uint8_t type;
  uint8_t a, b, c;
  float f;
} b3_cmd;

#define CMD_RING 256
static b3_cmd g_cmds[CMD_RING];
static uint32_t g_cmd_w = 0;
static uint32_t g_cmd_r = 0;

static void push_cmd(uint8_t type, uint8_t a, uint8_t b, uint8_t c, float f)
{
  uint32_t w = __atomic_load_n(&g_cmd_w, __ATOMIC_RELAXED);
  uint32_t r = __atomic_load_n(&g_cmd_r, __ATOMIC_ACQUIRE);
  if (((w + 1u) & (CMD_RING - 1u)) == r) return;
  b3_cmd *q = &g_cmds[w];
  q->type = type; q->a = a; q->b = b; q->c = c; q->f = f;
  VIBE_RELEASE();
  __atomic_store_n(&g_cmd_w, (w + 1u) & (CMD_RING - 1u), __ATOMIC_RELEASE);
}

/* ------------------------------------------------------------------ engine */
static struct b_instance g_inst;
static const b3_cmd *g_cmd; /* current command while rendering (not shared) */

static float g_bufA[BUFFER_SIZE_SAMPLES];
static float g_bufB[BUFFER_SIZE_SAMPLES];
static float g_bufC[BUFFER_SIZE_SAMPLES];
static float g_bufD0[BUFFER_SIZE_SAMPLES];
static float g_bufD1[BUFFER_SIZE_SAMPLES];
static float g_bufL0[BUFFER_SIZE_SAMPLES];
static float g_bufL1[BUFFER_SIZE_SAMPLES];
static int g_buf_off = BUFFER_SIZE_SAMPLES;

static float g_audio_sr = 48000.0f;
static float g_audio_master = 1.0f;
static b3_drum_ctrl g_audio_drums = { 0.55f, 1.0f, 1.0f, 0.08f, 0.5f, 0.0f };

/* SDK 11 mixer state, mirrored from the control flow through CMD_PARAM.
 * Slots 0..2 are the three organ manuals, slot 3 is the drum bus. */
static float g_mix_level[MIX_SLOTS] = { 1.0f, 1.0f, 1.0f, 0.55f };
static float g_mix_pan[MIX_SLOTS]   = { 0.0f, 0.0f, 0.0f, 0.0f };
static unsigned char g_mix_mute[MIX_SLOTS];
static unsigned char g_mix_solo[MIX_SLOTS];

static int g_audio_pattern = 0;
static int g_audio_run = 0;
static float g_audio_bpm = 112.0f;
static float g_audio_swing = 0.12f;
static int g_seq_step = 0;
static int g_seq_force = 1;
static double g_seq_phase = 0.0;
static uint64_t g_audio_samples = 0;
static int g_stab_frames = 20000;

static uint32_t g_audio_step = 0;
static uint32_t g_audio_running = 0;

#define MAX_STAB_KEYS 32
static unsigned char g_stab_key[MAX_STAB_KEYS];
static uint64_t g_stab_release[MAX_STAB_KEYS];
static int g_stab_count = 0;

/* ------------------------------------------------------------ engine setup */
static int vib_value(int choice)
{
  static const int map[7] = { 0, VIB1, CHO1, VIB2, CHO2, VIB3, CHO3 };
  if (choice < 0) choice = 0;
  if (choice > 6) choice = 6;
  return map[choice];
}

static int rotary_value(int choice)
{
  static const int map[3] = { WHIRL_STOP, WHIRL_SLOW, WHIRL_FAST };
  if (choice < 0) choice = 0;
  if (choice > 2) choice = 2;
  return map[choice];
}

static int map_note_to_key(int note, int channel)
{
  if (channel == 2) {
    if (note < 24 || note > 49) return -1;
    return 128 + note - 24;
  }
  if (note < 36 || note > 96) return -1;
  if (channel == 1) return 64 + note - 36;
  return note - 36;
}

/* Mixer fader state is applied together: a solo on any slot mutes the others.
 * The three organ gains live in the Beatrix tone generator; drums have their
 * own bus with the canonical pan from mix_4_pan. */
static void apply_mixer(void)
{
  int i, any_solo = 0;
  float g[MIX_SLOTS];

  for (i = 0; i < MIX_SLOTS; i++)
    if (g_mix_solo[i]) any_solo = 1;
  for (i = 0; i < MIX_SLOTS; i++) {
    g[i] = vd_clamp(g_mix_level[i], 0.0f, 1.0f);
    if (g_mix_mute[i] || (any_solo && !g_mix_solo[i])) g[i] = 0.0f;
  }

  if (g_inst.synth) {
    setManualGain(g_inst.synth, UPPER_MANUAL, g[0]);
    setManualGain(g_inst.synth, LOWER_MANUAL, g[1]);
    setManualGain(g_inst.synth, PEDAL_BOARD, g[2]);
  }
  g_audio_drums.gain = g[3];
  g_audio_drums.pan  = vd_clamp(g_mix_pan[3], -1.0f, 1.0f);
}

static void audio_apply_param(int idx, float v)
{
  if (!g_inst.synth) return;
  switch (idx) {
  case P_SWELL:
    g_inst.synth->swellPedalGain = g_inst.synth->outputLevelTrim * vd_clamp(v, 0.0f, 1.5f);
    break;
  case P_MASTER:
    g_audio_master = vd_clamp(v, 0.0f, 2.0f);
    break;
  case P_VIB:
    setVibrato(g_inst.synth, vib_value((int)v));
    break;
  case P_VIB_UPPER:
    setVibratoUpper(g_inst.synth, v >= 0.5f);
    break;
  case P_VIB_LOWER:
    setVibratoLower(g_inst.synth, v >= 0.5f);
    break;
  case P_PERC_ON:
    setPercussionEnabled(g_inst.synth, v >= 0.5f);
    break;
  case P_PERC_FAST:
    setPercussionFast(g_inst.synth, v >= 0.5f);
    break;
  case P_PERC_SOFT:
    setPercussionVolume(g_inst.synth, v >= 0.5f);
    break;
  case P_PERC_FIRST:
    setPercussionFirst(g_inst.synth, v >= 0.5f);
    break;
  case P_CLEAN:
    setClean(g_inst.preamp, v >= 0.5f);
    break;
  case P_DRIVE:
    fsetInputGain(g_inst.preamp, 0.15f + 0.85f * vd_clamp(v, 0.0f, 1.0f));
    break;
  case P_OUTPUT:
    fsetOutputGain(g_inst.preamp, vd_clamp(v, 0.0f, 1.0f));
    break;
  case P_REVERB:
    if (g_inst.reverb) setReverbMix(g_inst.reverb, vd_clamp(v, 0.0f, 1.0f));
    break;
  case P_ROTARY:
    if (g_inst.whirl) setRevSelect(g_inst.whirl, rotary_value((int)v));
    break;
  case P_RHYTHM:
    g_audio_pattern = (int)v & 7;
    g_seq_step = 0;
    g_seq_force = 1;
    g_seq_phase = 0.0;
    break;
  case P_RUN:
    g_audio_run = v >= 0.5f;
    g_seq_step = 0;
    g_seq_force = g_audio_run;
    g_seq_phase = 0.0;
    __atomic_store_n(&g_audio_running, (uint32_t)g_audio_run, __ATOMIC_RELAXED);
    break;
  case P_BPM:
    g_audio_bpm = vd_clamp(v, 20.0f, 400.0f);
    break;
  case P_SWING:
    g_audio_swing = vd_clamp(v, 0.0f, 0.75f);
    break;
  case P_DRUM_LEVEL:
  case P_MIX4_LEVEL:
    g_mix_level[3] = vd_clamp(v, 0.0f, 1.0f);
    apply_mixer();
    break;
  case P_STAB_LEN:
    g_stab_frames = (int)(vd_clamp(v, 0.05f, 2.0f) * g_audio_sr);
    break;

  case P_MIX1_LEVEL: g_mix_level[0] = vd_clamp(v, 0.0f, 1.0f); apply_mixer(); break;
  case P_MIX2_LEVEL: g_mix_level[1] = vd_clamp(v, 0.0f, 1.0f); apply_mixer(); break;
  case P_MIX3_LEVEL: g_mix_level[2] = vd_clamp(v, 0.0f, 1.0f); apply_mixer(); break;
  case P_MIX1_PAN:   g_mix_pan[0]   = vd_clamp(v, -1.0f, 1.0f); apply_mixer(); break;
  case P_MIX2_PAN:   g_mix_pan[1]   = vd_clamp(v, -1.0f, 1.0f); apply_mixer(); break;
  case P_MIX3_PAN:   g_mix_pan[2]   = vd_clamp(v, -1.0f, 1.0f); apply_mixer(); break;
  case P_MIX4_PAN:   g_mix_pan[3]   = vd_clamp(v, -1.0f, 1.0f); apply_mixer(); break;
  case P_MIX1_MUTE:  g_mix_mute[0]  = v >= 0.5f; apply_mixer(); break;
  case P_MIX2_MUTE:  g_mix_mute[1]  = v >= 0.5f; apply_mixer(); break;
  case P_MIX3_MUTE:  g_mix_mute[2]  = v >= 0.5f; apply_mixer(); break;
  case P_MIX4_MUTE:  g_mix_mute[3]  = v >= 0.5f; apply_mixer(); break;
  case P_MIX1_SOLO:  g_mix_solo[0]  = v >= 0.5f; apply_mixer(); break;
  case P_MIX2_SOLO:  g_mix_solo[1]  = v >= 0.5f; apply_mixer(); break;
  case P_MIX3_SOLO:  g_mix_solo[2]  = v >= 0.5f; apply_mixer(); break;
  case P_MIX4_SOLO:  g_mix_solo[3]  = v >= 0.5f; apply_mixer(); break;
  /* ARM is reported for controller/DAW compatibility; this firmware has no
   * recorder, and arming a slot never starts the drum sequencer. */
  case P_MIX1_ARM:
  case P_MIX2_ARM:
  case P_MIX3_ARM:
  case P_MIX4_ARM:
  case P_MIX_SELECT:
    break;
  default:
    break;
  }
}

static void audio_engine_init(float sr)
{
  int i;
  g_audio_sr = sr;
  SampleRateD = (double)sr;

  memset(&g_inst, 0, sizeof(g_inst));
  g_inst.reverb  = allocReverb();
  g_inst.whirl   = allocWhirl();
  g_inst.synth   = allocTonegen();
  g_inst.preamp  = allocPreamp();
  g_inst.midicfg = 0;

  if (!g_inst.synth || !g_inst.preamp || !g_inst.reverb || !g_inst.whirl) return;

  initToneGenerator(g_inst.synth, 0);
  initVibrato(g_inst.synth, 0);
  initPreamp(g_inst.preamp, 0);
  initReverb(g_inst.reverb, 0, SampleRateD);
  initWhirl(g_inst.whirl, 0, SampleRateD);

  setDrawBars(&g_inst, UPPER_MANUAL, defaultPresetUpperManual);
  setDrawBars(&g_inst, LOWER_MANUAL, defaultPresetLowerManual);
  setDrawBars(&g_inst, PEDAL_BOARD, defaultPresetPedalBoard);

  for (i = 0; i < P_COUNT; i++) audio_apply_param(i, g_p[i]);
  apply_mixer();
  for (i = 0; i < MAX_STAB_KEYS; i++) { g_stab_key[i] = 0; g_stab_release[i] = 0; }
  g_stab_count = 0;
  g_buf_off = BUFFER_SIZE_SAMPLES;

  b3_drums_init(sr);
  b3_drums_reset();
}

/* ---------------------------------------------------------- control helpers */
static void set_param(int idx, float v, int notify)
{
  const vibe_param *p;
  int k;
  if (idx < 0 || idx >= P_COUNT) return;
  p = &PARAMS[idx];
  if (p->steps > 1) {
    k = (int)(v - p->min + 0.5f);
    if (k < 0) k = 0;
    if (k >= p->steps) k = p->steps - 1;
    v = p->min + (float)k;
  } else {
    v = vd_clamp(v, p->min, p->max);
  }
  if (idx == P_PRESET && !g_applying_preset && v == g_p[idx]) { apply_preset((int)v); return; }
  /* The legacy drum_level and the SDK mix_4_level are one fader. If an older
   * saved state left them apart, even setting the same value re-syncs them. */
  if (v == g_p[idx] && !((idx == P_DRUM_LEVEL || idx == P_MIX4_LEVEL) &&
                         g_p[P_DRUM_LEVEL] != g_p[P_MIX4_LEVEL])) return;
  g_p[idx] = v;

  if (idx == P_PRESET && !g_applying_preset) {
    apply_preset((int)v);
    return;
  }

  if ((idx == P_DRUM_LEVEL || idx == P_MIX4_LEVEL) && !g_mirror_drum) {
    int other = (idx == P_DRUM_LEVEL) ? P_MIX4_LEVEL : P_DRUM_LEVEL;
    g_mirror_drum = 1;
    if (g_p[other] != v) set_param(other, v, notify);
    g_mirror_drum = 0;
  }

  if (idx >= P_U1 && idx <= P_U9) {
    g_db[0][idx - P_U1] = (unsigned char)v;
    push_cmd(CMD_DRAWBAR, UPPER_MANUAL, (uint8_t)(idx - P_U1), (uint8_t)v, 0.0f);
  } else if (idx >= P_L1 && idx <= P_L9) {
    g_db[1][idx - P_L1] = (unsigned char)v;
    push_cmd(CMD_DRAWBAR, LOWER_MANUAL, (uint8_t)(idx - P_L1), (uint8_t)v, 0.0f);
  } else if (idx >= P_P1 && idx <= P_P9) {
    g_db[2][idx - P_P1] = (unsigned char)v;
    push_cmd(CMD_DRAWBAR, PEDAL_BOARD, (uint8_t)(idx - P_P1), (uint8_t)v, 0.0f);
  } else {
    push_cmd(CMD_PARAM, (uint8_t)idx, 0, 0, v);
    if (idx == P_BPM) vibe_tempo_set(v);
  }

  if (notify) vibe_param_set(idx, v);
}

/* Load a whole B3 setup. set_param() updates the audio flow and the Editor;
 * the guard keeps the preset parameter itself from re-entering this function. */
static void apply_preset(int idx)
{
  const b3_preset *q;
  int i;
  if (idx < 0) idx = 0;
  if (idx >= NPRESET) idx = NPRESET - 1;
  q = &PRESETS[idx];

  g_applying_preset = 1;
  g_p[P_PRESET] = (float)idx;
  vibe_param_set(P_PRESET, (float)idx);

  for (i = 0; i < 9; i++) {
    set_param(P_U1 + i, q->u[i], 1);
    set_param(P_L1 + i, q->l[i], 1);
    set_param(P_P1 + i, q->p[i], 1);
  }
  set_param(P_VIB, q->vib, 1);
  set_param(P_VIB_UPPER, q->vib_u, 1);
  set_param(P_VIB_LOWER, q->vib_l, 1);
  set_param(P_PERC_ON, q->perc, 1);
  set_param(P_PERC_FAST, q->perc_fast, 1);
  set_param(P_PERC_SOFT, q->perc_soft, 1);
  set_param(P_PERC_FIRST, q->perc_first, 1);
  set_param(P_CLEAN, q->clean, 1);
  set_param(P_DRIVE, q->drive, 1);
  set_param(P_OUTPUT, q->out, 1);
  set_param(P_REVERB, q->reverb, 1);
  set_param(P_SWELL, q->swell, 1);
  set_param(P_MASTER, q->master, 1);
  set_param(P_ROTARY, q->rotary, 1);
  set_param(P_RHYTHM, q->rhythm, 1);
  set_param(P_BPM, q->bpm, 1);
  set_param(P_SWING, q->swing, 1);
  set_param(P_DRUM_LEVEL, q->drum_level, 1);
  set_param(P_CHORD, q->chord, 1);
  g_applying_preset = 0;
}

static void cycle_preset(int dir)
{
  int n = (int)g_p[P_PRESET] + dir;
  if (n < 0) n = NPRESET - 1;
  else if (n >= NPRESET) n = 0;
  set_param(P_PRESET, (float)n, 1);
}

static void bump_param(int idx, float detents)
{
  const vibe_param *p = &PARAMS[idx];
  float step = (p->steps > 1) ? 1.0f : (p->max - p->min) / 100.0f;
  set_param(idx, g_p[idx] + detents * step, 1);
}

static void toggle_param(int idx)
{
  set_param(idx, g_p[idx] >= 0.5f ? 0.0f : 1.0f, 1);
}

static void toggle_run(void)
{
  toggle_param(P_RUN);
}

static int mix_slot(void)
{
  int s = (int)g_p[P_MIX_SELECT] - 1;
  if (s < 0) s = 0;
  else if (s >= MIX_SLOTS) s = MIX_SLOTS - 1;
  return s;
}

static void select_rhythm(int pat, int start)
{
  if (pat < 0 || pat >= B3_RHYTHMS) return;
  if (start && (int)g_p[P_RHYTHM] == pat && g_p[P_RUN] >= 0.5f) {
    set_param(P_RUN, 0.0f, 1);
    return;
  }
  set_param(P_RHYTHM, (float)pat, 1);
  if (start) set_param(P_RUN, 1.0f, 1);
}

static void trigger_chord(int type)
{
  if (type < 0 || type >= 8) return;
  set_param(P_CHORD, (float)type, 1);
  g_last_chord = type;
  push_cmd(CMD_STAB, (uint8_t)g_root_note, (uint8_t)type,
           (uint8_t)(g_stab_frames > 0 ? 1 : 0), 0.0f);
}

static void panic_all(void)
{
  int c, n;
  for (c = 0; c < 16; c++)
    for (n = 0; n < 128; n++) g_held[c][n] = -1;
  push_cmd(CMD_PANIC, 0, 0, 0, 0.0f);
}

static void note_event(const vibe_event *e)
{
  int note = (int)e->id;
  int ch = (int)(e->midi[0] & 15);
  int key = map_note_to_key(note, ch);
  if (key < 0 || key >= MAX_KEYS) return;
  if (e->down) {
    if (g_held[ch][note] >= 0)
      push_cmd(CMD_NOTE_OFF, (uint8_t)g_held[ch][note], (uint8_t)g_held[ch][note], 0, 0.0f);
    g_held[ch][note] = (short)key;
    push_cmd(CMD_NOTE_ON, (uint8_t)key, (uint8_t)key,
             (uint8_t)(e->value > 0.75f ? 127 : 100), 0.0f);
    g_root_note = note;
  } else {
    key = g_held[ch][note];
    if (key >= 0) {
      push_cmd(CMD_NOTE_OFF, (uint8_t)key, (uint8_t)key, 0, 0.0f);
      g_held[ch][note] = -1;
    }
  }
}

/* ------------------------------------------------------------------- audio */
static void process_commands(void)
{
  uint32_t r = __atomic_load_n(&g_cmd_r, __ATOMIC_RELAXED);
  uint32_t w = __atomic_load_n(&g_cmd_w, __ATOMIC_ACQUIRE);
  while (r != w) {
    b3_cmd cmd = g_cmds[r];
    int i, key, note;
    r = (r + 1u) & (CMD_RING - 1u);
    g_cmd = &cmd;
    switch (cmd.type) {
    case CMD_NOTE_ON:
      oscKeyOn(g_inst.synth, cmd.a, cmd.b);
      break;
    case CMD_NOTE_OFF:
      oscKeyOff(g_inst.synth, cmd.a, cmd.b);
      break;
    case CMD_DRAWBAR: {
      unsigned int bars[9];
      int j;
      for (j = 0; j < 9; j++) bars[j] = defaultPresetUpperManual[j];
      if (cmd.a == UPPER_MANUAL) {
        for (j = 0; j < 9; j++) bars[j] = (unsigned int)g_db[0][j];
        bars[cmd.b] = cmd.c;
        for (j = 0; j < 9; j++) g_db[0][j] = (unsigned char)bars[j];
        setDrawBars(&g_inst, UPPER_MANUAL, bars);
      } else if (cmd.a == LOWER_MANUAL) {
        for (j = 0; j < 9; j++) bars[j] = (unsigned int)g_db[1][j];
        bars[cmd.b] = cmd.c;
        for (j = 0; j < 9; j++) g_db[1][j] = (unsigned char)bars[j];
        setDrawBars(&g_inst, LOWER_MANUAL, bars);
      } else {
        for (j = 0; j < 9; j++) bars[j] = (unsigned int)g_db[2][j];
        bars[cmd.b] = cmd.c;
        for (j = 0; j < 9; j++) g_db[2][j] = (unsigned char)bars[j];
        setDrawBars(&g_inst, PEDAL_BOARD, bars);
      }
      break;
    }
    case CMD_PARAM:
      audio_apply_param(cmd.a, cmd.f);
      break;
    case CMD_PANIC:
      for (i = 0; i < MAX_KEYS; i++) oscKeyOff(g_inst.synth, (unsigned char)i, (unsigned char)i);
      g_stab_count = 0;
      b3_drums_reset();
      break;
    case CMD_RHYTHM:
      g_audio_pattern = cmd.a & 7;
      g_seq_step = 0;
      g_seq_force = 1;
      g_seq_phase = 0.0;
      break;
    case CMD_RUN:
      g_audio_run = cmd.a != 0;
      g_seq_step = 0;
      g_seq_force = g_audio_run;
      g_seq_phase = 0.0;
      __atomic_store_n(&g_audio_running, (uint32_t)g_audio_run, __ATOMIC_RELAXED);
      break;
    case CMD_STAB:
      note = cmd.a;
      if (note < 48) note = 60;
      if (note > 84) note = 60;
      for (i = 0; i < 5; i++) {
        int iv = CHORD_INTERVALS[cmd.b & 7][i];
        if (iv < 0) break;
        key = map_note_to_key(note + iv, 0);
        if (key < 0 || key >= 64) continue;
        oscKeyOn(g_inst.synth, (unsigned char)key, (unsigned char)key);
        if (g_stab_count < MAX_STAB_KEYS) {
          int k;
          for (k = 0; k < g_stab_count; k++)
            if (g_stab_key[k] == (unsigned char)key) break;
          if (k == g_stab_count) g_stab_count++;
          g_stab_key[k] = (unsigned char)key;
          g_stab_release[k] = g_audio_samples + (uint64_t)g_stab_frames;
        }
      }
      break;
    default:
      break;
    }
  }
  g_cmd = 0;
  VIBE_ACQUIRE();
  __atomic_store_n(&g_cmd_r, r, __ATOMIC_RELEASE);
}

static void release_stabs(void)
{
  int i = 0;
  while (i < g_stab_count) {
    if (g_audio_samples >= g_stab_release[i]) {
      oscKeyOff(g_inst.synth, g_stab_key[i], g_stab_key[i]);
      g_stab_count--;
      if (i < g_stab_count) {
        g_stab_key[i] = g_stab_key[g_stab_count];
        g_stab_release[i] = g_stab_release[g_stab_count];
      }
    } else i++;
  }
}

static void trigger_step(int step)
{
  unsigned char bits = b3_drum_step(g_audio_pattern, step);
  int vel = (step % 4 == 0) ? 112 : 78;
  int v;
  static const int voices[8] = {
    B3_D_KICK, B3_D_SNARE, B3_D_RIM, B3_D_HAT,
    B3_D_OPEN, B3_D_RIDE, B3_D_TOM, B3_D_BRUSH
  };
  for (v = 0; v < 8; v++)
    if (bits & (1u << v)) b3_drums_trigger(voices[v], v == B3_D_RIDE ? vel - 18 : vel);
}

static void update_sequencer(int frames)
{
  int next = g_seq_step;
  if (!g_audio_run) {
    __atomic_store_n(&g_audio_step, (uint32_t)g_seq_step, __ATOMIC_RELAXED);
    return;
  }

  {
    const vibe_transport_info *t = vibe_transport();
    if (t && t->has && t->playing && t->bpm > 0.0f) {
      double rel = t->beat - t->bar_start;
      if (rel < 0.0) rel = 0.0;
      next = (int)(rel * 4.0) & 15;
    } else {
      double rate = (double)g_audio_bpm * 4.0 / 60.0;
      float sw = (g_seq_step & 1) ? -g_audio_swing : g_audio_swing;
      g_seq_phase += (double)frames * rate / (double)g_audio_sr * (1.0 + (double)sw);
      while (g_seq_phase >= 1.0) {
        g_seq_phase -= 1.0;
        next = (next + 1) & 15;
      }
    }
  }

  if (g_seq_force || next != g_seq_step) {
    g_seq_step = next;
    g_seq_force = 0;
    trigger_step(g_seq_step);
    __atomic_store_n(&g_audio_step, (uint32_t)g_seq_step, __ATOMIC_RELAXED);
  }
}

static void render_engine_block(void)
{
  oscGenerateFragment(g_inst.synth, g_bufA, BUFFER_SIZE_SAMPLES);
  preamp(g_inst.preamp, g_bufA, g_bufB, BUFFER_SIZE_SAMPLES);
  reverb(g_inst.reverb, g_bufB, g_bufC, BUFFER_SIZE_SAMPLES);
  whirlProc3(g_inst.whirl, g_bufC,
             g_bufL0, g_bufL1, g_bufD0, g_bufD1, BUFFER_SIZE_SAMPLES);
}

/* -------------------------------------------------------------------- FOCUS */
#define FB_PLAY  200
#define FB_STOP  201
#define FB_PANIC 202
#define FB_PAT   300
#define FB_CHORD 400

static void focus_value(int idx, int x, int y, int w, int h)
{
  int id = idx + 1;
  const vibe_param *p = &PARAMS[idx];
  g_focus_val[id] = g_p[idx];
  vf_value(id, x, y, w, h, &g_focus_val[id], p->min, p->max,
           (p->steps > 1) ? 1.0f : (p->max - p->min) / 100.0f);
}

/* ------------------------------------------------------------------- drawing */
/* The Leslie motif in the header: a two-horn rotor over a vented cabinet. */
static void draw_leslie(int x, int y, unsigned int tick)
{
  float ph = (float)tick * 0.035f;
  int dx = (int)(8.0f * vd_sin(ph));
  int dy = (int)(4.0f * vd_sin(ph + 0.25f));
  int i;
  vg_rect(x + 2, y + 6, 26, 15, COL_PANEL2);
  vg_frame(x + 2, y + 6, 26, 15, 1, COL_PURPLE);
  vg_line(x + 15 + dx, y + 5 + dy, x + 15 - dx, y + 5 - dy, 2, COL_LILAC);
  vg_circle(x + 15 + dx, y + 5 + dy, 3, COL_PURPLE);
  vg_circle(x + 15 - dx, y + 5 - dy, 3, COL_LILAC);
  vg_circle(x + 15, y + 5, 2, COL_CREAM);
  for (i = 0; i < 3; i++)
    vg_line(x + 6 + i * 8, y + 10, x + 6 + i * 8, y + 18, 1, COL_MUTED);
}

static void header(const char *title, const char *sub)
{
  char b[16];
  int pi = (int)g_p[P_PRESET];
  vg_clear(COL_BG);
  vg_rect(0, 0, 480, 52, COL_PANEL);
  vg_rect(0, 50, 480, 2, COL_PURPLE);
  vg_text(title, 14, 8, VG_BIG, COL_LILAC, VG_LEFT);
  vg_text(sub, 14, 36, VG_SMALL, COL_MUTED, VG_LEFT);
  vg_itoa(pi + 1, b);
  vg_text(b, 444, 9, VG_SMALL, COL_LILAC, VG_RIGHT);
  vg_text("/24", 466, 9, VG_SMALL, COL_DIM, VG_RIGHT);
  vg_text(PRESET_NAMES[pi], 466, 31, VG_SMALL, COL_PURPLE, VG_RIGHT);
  draw_leslie(316, 30, g_draw_tick);
}

static void softbar(void)
{
  static const char *const names[PAGE_COUNT] = {
    "SOUND", "LOWER", "VIB", "FX", "RHY", "SEQ"
  };
  int i;
  vg_rect(0, 282, 480, 38, COL_PANEL);
  vg_rect(0, 282, 480, 1, COL_LINE);
  for (i = 0; i < 6; i++) {
    int x = i * 80;
    vg_rect(x + 3, 287, 74, 27, i == g_page ? COL_PURPLE : COL_PANEL2);
    vg_text(names[i], x + 40, 292, VG_SMALL,
            i == g_page ? COL_BG : COL_CREAM, VG_CENTER);
  }
  vg_text("HOME: controls", 8, 264, VG_SMALL, COL_DIM, VG_LEFT);
}

static void panel(int x, int y, int w, int h, const char *title)
{
  vg_round(x, y, w, h, 8, COL_PANEL);
  vg_frame(x, y, w, h, 1, COL_LINE);
  if (title) vg_text(title, x + 10, y + 7, VG_SMALL, COL_LILAC, VG_LEFT);
}

static void draw_bars(int y, int first, const char *label, int compact)
{
  int i;
  int x0 = 18;
  int step = compact ? 50 : 50;
  int top = y + 32;
  int h = compact ? 88 : 150;
  vg_text(label, 18, y + 8, VG_BOLD, COL_CREAM, VG_LEFT);
  for (i = 0; i < 9; i++) {
    int x = x0 + i * step;
    int v = (int)g_db[first][i];
    int bh = (v * h) / 8;
    vg_rect(x, top, 34, h, COL_BG);
    vg_frame(x, top, 34, h, 1, COL_LINE);
    if (bh > 0) vg_rect(x + 2, top + h - bh, 30, bh, i < 3 ? COL_PURPLE : COL_LILAC);
    {
      char b[4];
      vg_itoa(v, b);
      vg_text(b, x + 17, top + h + 4, VG_SMALL, COL_CREAM, VG_CENTER);
    }
    focus_value(first == 0 ? P_U1 + i : (first == 1 ? P_L1 + i : P_P1 + i),
                x, top, 34, h);
  }
}

static void draw_toggle(int x, int y, int w, int idx, const char *name)
{
  int on = g_p[idx] >= 0.5f;
  vg_round(x, y, w, 30, 5, on ? COL_PURPLE : COL_PANEL2);
  vg_frame(x, y, w, 30, 1, on ? COL_LILAC : COL_LINE);
  vg_text(name, x + 8, y + 7, VG_SMALL, on ? COL_BG : COL_MUTED, VG_LEFT);
  vg_text(on ? "ON" : "OFF", x + w - 8, y + 7, VG_SMALL,
          on ? COL_BG : COL_DIM, VG_RIGHT);
  focus_value(idx, x, y, w, 30);
}

static void draw_param(int x, int y, int w, int idx)
{
  const vibe_param *p = &PARAMS[idx];
  char b[24];
  float norm = (g_p[idx] - p->min) / ((p->max - p->min) > 0 ? (p->max - p->min) : 1);
  vg_text(p->name, x, y, VG_SMALL, COL_MUTED, VG_LEFT);
  vg_rect(x, y + 18, w, 8, COL_BG);
  vg_frame(x, y + 18, w, 8, 1, COL_LINE);
  vg_rect(x + 1, y + 19, (int)((w - 2) * vd_clamp(norm, 0.f, 1.f)), 6,
          idx == P_REVERB ? COL_BLUE : COL_LILAC);
  if (p->steps > 1) {
    int k = (int)(g_p[idx] + 0.5f);
    if (k >= 0 && k < p->steps && p->choices)
      vg_text(p->choices[k], x + w, y, VG_SMALL, COL_CREAM, VG_RIGHT);
    else
      vg_text("-", x + w, y, VG_SMALL, COL_CREAM, VG_RIGHT);
  } else {
    vg_ftoa(g_p[idx], 2, b);
    vg_text(b, x + w, y, VG_SMALL, COL_CREAM, VG_RIGHT);
  }
  focus_value(idx, x, y, w, 28);
}

static void draw_page_sound(void)
{
  header("UPPER MANUAL", "Drawbars - pads 9-16: chord stabs");
  panel(10, 58, 460, 214, "DRAWBARS 1-9");
  draw_bars(74, 0, "UPPER", 0);
  panel(330, 58, 140, 214, "LEVELS");
  draw_param(344, 92, 112, P_SWELL);
  draw_param(344, 132, 112, P_MASTER);
  draw_param(344, 172, 112, P_DRIVE);
  draw_param(344, 212, 112, P_REVERB);
}

static void draw_page_lower(void)
{
  header("LOWER & PEDAL", "MIDI ch 2 lower - ch 3 pedal");
  panel(10, 58, 460, 120, "LOWER MANUAL");
  draw_bars(68, 1, "LOWER", 1);
  panel(10, 186, 460, 86, "PEDAL BOARD");
  draw_bars(200, 2, "PEDAL", 1);
}

static void draw_page_vib(void)
{
  header("VIBRATO & PERCUSSION", "Scanner chorus + percussion");
  panel(10, 58, 220, 214, "SCANNER");
  draw_param(24, 94, 192, P_VIB);
  draw_toggle(24, 140, 92, P_VIB_UPPER, "UPPER");
  draw_toggle(124, 140, 92, P_VIB_LOWER, "LOWER");
  vg_text("VIB 1-3 / CHORUS 1-3", 24, 190, VG_SMALL, COL_DIM, VG_LEFT);
  vg_text("Beatrix scanner circuit", 24, 212, VG_SMALL, COL_DIM, VG_LEFT);
  panel(240, 58, 230, 214, "PERCUSSION");
  draw_toggle(254, 94, 202, P_PERC_ON, "PERCUSSION");
  draw_toggle(254, 132, 96, P_PERC_FAST, "FAST");
  draw_toggle(360, 132, 96, P_PERC_SOFT, "SOFT");
  draw_toggle(254, 170, 202, P_PERC_FIRST, "FIRST / SECOND");
  vg_text("Classic 2nd-harmonic B3 snap", 254, 220, VG_SMALL, COL_DIM, VG_LEFT);
}

static void draw_page_fx(void)
{
  int slot = mix_slot();
  header("FX · 4-SLOT MIXER", "Upper / lower / pedal / drums");
  panel(10, 58, 220, 120, "PREAMP");
  draw_toggle(24, 88, 192, P_CLEAN, "CLEAN MODE");
  draw_param(24, 126, 192, P_DRIVE);
  panel(240, 58, 230, 120, "SPACE · OUTPUT");
  draw_param(254, 88, 96, P_REVERB);
  draw_param(356, 88, 100, P_OUTPUT);
  draw_param(254, 126, 96, P_ROTARY);
  draw_param(356, 126, 100, P_MASTER);
  panel(10, 186, 460, 86, "MIXER · FOUR LEVELS");
  draw_param(24, 210, 96, MIX_LEVEL(0));
  draw_param(136, 210, 96, MIX_LEVEL(1));
  draw_param(248, 210, 96, MIX_LEVEL(2));
  draw_param(360, 210, 96, MIX_LEVEL(3));
  vg_text("SEL", 24, 244, VG_SMALL, COL_DIM, VG_LEFT);
  draw_param(58, 240, 96, P_MIX_SELECT);
  draw_param(164, 240, 96, MIX_PAN(slot));
  draw_toggle(270, 240, 76, MIX_MUTE(slot), "MUTE");
  draw_toggle(354, 240, 76, MIX_SOLO(slot), "SOLO");
}

static void draw_page_rhythm(void)
{
  int i, step;
  header("RHYTHM PADS 1-8", "Drums stay off until you opt in");
  panel(10, 58, 250, 214, "GROOVES");
  for (i = 0; i < 8; i++) {
    int x = 22 + (i & 1) * 118;
    int y = 92 + (i >> 1) * 42;
    int on = ((int)g_p[P_RHYTHM] == i);
    vg_round(x, y, 110, 34, 5, on ? COL_PURPLE : COL_PANEL2);
    vg_frame(x, y, 110, 34, 1, on ? COL_LILAC : COL_LINE);
    vg_text(CH_RHYTHM[i], x + 55, y + 10, VG_SMALL,
            on ? COL_BG : COL_CREAM, VG_CENTER);
    vf_button(FB_PAT + i, x, y, 110, 34);
  }
  draw_toggle(22, 228, 110, P_RUN, "DRUMS");
  draw_param(140, 228, 108, P_DRUM_LEVEL);

  panel(270, 58, 200, 214, "16 STEPS");
  step = (int)__atomic_load_n(&g_audio_step, __ATOMIC_RELAXED) & 15;
  for (i = 0; i < 16; i++) {
    int x = 282 + (i & 3) * 45;
    int y = 96 + (i >> 2) * 36;
    unsigned char b = b3_drum_step((int)g_p[P_RHYTHM], i);
    vg_rect(x, y, 38, 28, i == step ? COL_PURPLE : (b ? COL_PANEL2 : COL_BG));
    vg_frame(x, y, 38, 28, 1, b ? COL_LILAC : COL_LINE);
    {
      char t[4];
      t[0] = b ? '*' : '.'; t[1] = 0;
      vg_text(t, x + 19, y + 8, VG_SMALL, b ? COL_CREAM : COL_DIM, VG_CENTER);
    }
  }
  vg_text("DAW PLAY = tempo only", 282, 244, VG_SMALL, COL_DIM, VG_LEFT);
}

static void draw_page_seq(void)
{
  int i;
  const char *names[8] = { "MAJ", "MIN", "7TH", "M7", "MAJ7", "SUS4", "DIM", "6/9" };
  int playing = (int)__atomic_load_n(&g_audio_running, __ATOMIC_RELAXED);
  int step = (int)__atomic_load_n(&g_audio_step, __ATOMIC_RELAXED) & 15;
  header("SEQUENCER & CHORD STABS", "Step grid - pads 9-16: stabs");
  panel(10, 58, 220, 214, "TRANSPORT");
  draw_param(24, 92, 192, P_BPM);
  draw_param(24, 134, 192, P_SWING);
  draw_param(24, 176, 192, P_STAB_LEN);
  {
    int x = 24, y = 222, w = 92;
    vg_round(x, y, w, 32, 5, playing ? COL_GREEN : COL_PURPLE);
    vg_text(playing ? "STOP" : "PLAY", x + w / 2, y + 9, VG_SMALL, COL_BG, VG_CENTER);
    vf_button(FB_PLAY, x, y, w, 32);
    x = 124;
    vg_round(x, y, w, 32, 5, COL_PANEL2);
    vg_text("PANIC", x + w / 2, y + 9, VG_SMALL, COL_CREAM, VG_CENTER);
    vf_button(FB_PANIC, x, y, w, 32);
  }
  panel(240, 58, 230, 214, "CHORDS");
  for (i = 0; i < 8; i++) {
    int x = 252 + (i & 1) * 106;
    int y = 92 + (i >> 1) * 39;
    int on = ((int)g_p[P_CHORD] == i);
    vg_round(x, y, 98, 30, 5, on ? COL_PURPLE : COL_PANEL2);
    vg_frame(x, y, 98, 30, 1, on ? COL_LILAC : COL_LINE);
    vg_text(names[i], x + 49, y + 8, VG_SMALL, on ? COL_BG : COL_CREAM, VG_CENTER);
    vf_button(FB_CHORD + i, x, y, 98, 30);
  }
  {
    char s[24];
    vg_itoa(step + 1, s);
    vg_text("STEP", 252, 248, VG_SMALL, COL_DIM, VG_LEFT);
    vg_text(s, 458, 244, VG_BIG, COL_LILAC, VG_RIGHT);
  }
}

static void draw_page(void)
{
  switch (g_page) {
  case PAGE_LOWER: draw_page_lower(); break;
  case PAGE_VIB:   draw_page_vib(); break;
  case PAGE_FX:    draw_page_fx(); break;
  case PAGE_RHY:   draw_page_rhythm(); break;
  case PAGE_SEQ:   draw_page_seq(); break;
  default:         draw_page_sound(); break;
  }
}

static const char *const HELP_ROWS[] = {
  "PRESETS", "SHIFT + A/B or left/right browses 24 B3 setups",
  "KEYS", "upper manual notes; MIDI ch 2 lower, ch 3 pedal",
  "PADS 1-8", "opt in and start a groove; tap again to stop",
  "PADS 9-16", "major, minor, 7, m7, maj7, sus4, dim, 6/9 stabs",
  "VE 1-4", "edit the four values of the open page",
  "VE A/B", "rhythm / chord selection",
  "VE DATA", "FOCUS: walk controls, ENTER to edit",
  "F1-F6", "open the six pages",
  "K1-K6", "SOUND, LOWER, VIB, FX, RHY, SEQ",
  "FX PAGE", "four mixer slots: upper, lower, pedal, drums",
  "PLAY / STOP", "runs drums only after you have opted in",
  "REC", "all notes off, reset stabs and drums",
  "HOME", "this overview; RETURN closes it",
  "OCT -/+", "the host shifts the keyboard octave"
};

static void draw_focus_frame(void)
{
  vf_begin();
  draw_page();
  vf_end(COL_PURPLE);
}

/* ---------------------------------------------------------------- lifecycle */
void vibe_on_init(float sample_rate)
{
  int i, n;

  for (i = 0; i < P_COUNT; i++) g_p[i] = PARAMS[i].def;
  for (i = 0; i < 3; i++)
    for (n = 0; n < 9; n++) g_db[i][n] = (unsigned char)PARAMS[P_U1 + i * 9 + n].def;
  for (i = 0; i < 16; i++)
    for (n = 0; n < 128; n++) g_held[i][n] = -1;

  n = vibe_store_read("b3-state", g_p, (int)sizeof(g_p));
  if (n == (int)sizeof(g_p)) {
    for (i = 0; i < 9; i++) {
      g_db[0][i] = (unsigned char)g_p[P_U1 + i];
      g_db[1][i] = (unsigned char)g_p[P_L1 + i];
      g_db[2][i] = (unsigned char)g_p[P_P1 + i];
    }
  }
  /* Drums are never inherited from saved state or a host transport. */
  g_p[P_RUN] = 0.0f;
  g_p[P_MIX4_LEVEL] = g_p[P_DRUM_LEVEL];

  vibe_params(PARAMS, P_COUNT);
  vibe_param_groups(GROUPS);
  vibe_editor_cards(CARDS, (int)(sizeof(CARDS) / sizeof(CARDS[0])));
  vibe_editor_accent(COL_PURPLE);
  vibe_editor_tab_color("A", COL_PURPLE);
  vibe_editor_tab_color("B", COL_LILAC);
  vibe_editor_tab_color("C", COL_CREAM);
  vibe_editor_tab_color("D", COL_BLUE);
  vibe_editor_tab_color("E", COL_GREEN);
  vibe_editor_tab_color("F", COL_FMIX);

  vibe_editor_control(P_RUN, VIBE_CTL_TOGGLE);
  for (i = 0; i < MIX_SLOTS; i++) {
    vibe_editor_control(MIX_PAN(i), VIBE_CTL_BIPOLAR);
    vibe_editor_control(MIX_MUTE(i), VIBE_CTL_TOGGLE);
    vibe_editor_control(MIX_SOLO(i), VIBE_CTL_TOGGLE);
    vibe_editor_control(MIX_ARM(i), VIBE_CTL_TOGGLE);
  }
  vibe_editor_control(MIX_LAST, VIBE_CTL_MENU);
  vibe_param_help(P_RUN, "Drums start only when you tap a pad or turn this on. DAW Play stays tempo-only.");
  vibe_param_help(P_DRUM_LEVEL, "Drum fader; mirrors mix_4_level. Drums remain silent until started.");
  vibe_param_help(P_MIX1_LEVEL, "Organ 1 (upper manual) fader in the four-slot mixer");
  vibe_param_help(P_MIX2_LEVEL, "Organ 2 (lower manual) fader in the four-slot mixer");
  vibe_param_help(P_MIX3_LEVEL, "Organ 3 (pedal board) fader in the four-slot mixer");
  vibe_param_help(P_MIX4_LEVEL, "Drums fader; mirrors the legacy drum_level parameter");
  vibe_param_help(MIX_LAST, "Choose the slot edited by a mixer controller");

  vibe_notes(VIBE_NOTES);
  vf_mode(VF_ALWAYS);
  vf_clicks(1);
  vibe_tempo_set(g_p[P_BPM]);

  vibe_label(VB_F1, "SOUND");
  vibe_label(VB_F2, "LOWER");
  vibe_label(VB_F3, "VIB");
  vibe_label(VB_F4, "FX");
  vibe_label(VB_F5, "RHY");
  vibe_label(VB_F6, "SEQ");
  vibe_label(VB_K1, "SOUND");
  vibe_label(VB_K2, "DRAW");
  vibe_label(VB_K3, "VIB");
  vibe_label(VB_K4, "FX");
  vibe_label(VB_K5, "RHY");
  vibe_label(VB_K6, "SEQ");

  audio_engine_init(sample_rate);
}

static void handle_focus(int r, int id)
{
  if (r == VF_CHANGED) {
    int idx = id - 1;
    if (idx >= 0 && idx < P_COUNT) set_param(idx, g_focus_val[id], 1);
  } else if (r == VF_PRESS) {
    if (id == FB_PLAY) toggle_run();
    else if (id == FB_STOP) set_param(P_RUN, 0.0f, 1);
    else if (id == FB_PANIC) panic_all();
    else if (id >= FB_PAT && id < FB_PAT + B3_RHYTHMS) select_rhythm(id - FB_PAT, 1);
    else if (id >= FB_CHORD && id < FB_CHORD + 8) trigger_chord(id - FB_CHORD);
  }
}

void vibe_on_event(const vibe_event *e)
{
  int id = -1;
  int r = vf_event(e, &id);
  if (r != VF_NO) {
    handle_focus(r, id);
    return;
  }
  if (e->type == VIBE_EV_BUTTON && e->id == VB_SHIFT) {
    g_shift = e->down;
    return;
  }

  switch (e->type) {
  case VIBE_EV_NOTE:
    note_event(e);
    break;

  case VIBE_EV_PAD:
    if (!e->down) break;
    if (e->id < 8) select_rhythm((int)e->id, 1);
    else trigger_chord((int)e->id - 8);
    break;

  case VIBE_EV_PARAM:
    if (e->id < P_COUNT) set_param((int)e->id, e->value, 0);
    break;

  case VIBE_EV_BUTTON:
    if (!e->down) break;
    if (g_shift && (e->id == VB_LEFT || e->id == VB_MENU || e->id == VB_RIGHT)) {
      cycle_preset(e->id == VB_LEFT ? -1 : 1);
      break;
    }
    switch (e->id) {
    case VB_HOME:
      g_help = !g_help;
      break;
    case VB_RETURN:
      if (g_help) g_help = 0;
      else g_page = PAGE_SOUND;
      break;
    case VB_MENU:
    case VB_RIGHT:
      g_help = 0;
      g_page = (g_page + 1) % PAGE_COUNT;
      break;
    case VB_LEFT:
      g_help = 0;
      g_page = (g_page + PAGE_COUNT - 1) % PAGE_COUNT;
      break;
    case VB_F1: g_page = PAGE_SOUND; break;
    case VB_F2: g_page = PAGE_LOWER; break;
    case VB_F3: g_page = PAGE_VIB; break;
    case VB_F4: g_page = PAGE_FX; break;
    case VB_F5: g_page = PAGE_RHY; break;
    case VB_F6: g_page = PAGE_SEQ; break;
    case VB_K1: g_page = PAGE_SOUND; break;
    case VB_K2: g_page = PAGE_LOWER; break;
    case VB_K3: g_page = PAGE_VIB; break;
    case VB_K4: g_page = PAGE_FX; break;
    case VB_K5: g_page = PAGE_RHY; break;
    case VB_K6: g_page = PAGE_SEQ; break;
    case VB_PLAY: toggle_run(); break;
    case VB_STOP: set_param(P_RUN, 0.0f, 1); break;
    case VB_REC:  panic_all(); break;
    default: break;
    }
    break;

  case VIBE_EV_ENCODER: {
    float d = e->value;
    if (d == 0.0f) break;
    if (g_shift && (e->id == VE_A || e->id == VE_B)) {
      cycle_preset(d > 0.0f ? 1 : -1);
      break;
    }
    switch (e->id) {
    case VE_1:
      if (g_page == PAGE_SOUND) bump_param(P_U1 + ((int)g_p[P_RHYTHM] & 7), d);
      else if (g_page == PAGE_LOWER) bump_param(P_L1 + ((int)g_p[P_RHYTHM] & 7), d);
      else if (g_page == PAGE_VIB) bump_param(P_VIB, d);
      else if (g_page == PAGE_FX) bump_param(MIX_LEVEL(mix_slot()), d);
      else if (g_page == PAGE_RHY) bump_param(P_DRUM_LEVEL, d);
      else bump_param(P_BPM, d);
      break;
    case VE_2:
      if (g_page == PAGE_SOUND) bump_param(P_MASTER, d);
      else if (g_page == PAGE_LOWER) bump_param(P_P1 + ((int)g_p[P_RHYTHM] & 7), d);
      else if (g_page == PAGE_VIB) bump_param(P_PERC_ON, d);
      else if (g_page == PAGE_FX) bump_param(MIX_PAN(mix_slot()), d);
      else if (g_page == PAGE_RHY) bump_param(P_SWING, d);
      else bump_param(P_SWING, d);
      break;
    case VE_3:
      if (g_page == PAGE_SOUND) bump_param(P_SWELL, d);
      else if (g_page == PAGE_LOWER) bump_param(P_MASTER, d);
      else if (g_page == PAGE_VIB) bump_param(P_PERC_FAST, d);
      else if (g_page == PAGE_FX) bump_param(P_REVERB, d);
      else if (g_page == PAGE_RHY) bump_param(P_BPM, d);
      else bump_param(P_STAB_LEN, d);
      break;
    case VE_4:
      if (g_page == PAGE_SOUND) bump_param(P_DRIVE, d);
      else if (g_page == PAGE_LOWER) bump_param(P_SWELL, d);
      else if (g_page == PAGE_VIB) bump_param(P_VIB_UPPER, d);
      else if (g_page == PAGE_FX) bump_param(P_ROTARY, d);
      else if (g_page == PAGE_RHY) bump_param(P_RHYTHM, d);
      else bump_param(P_CHORD, d);
      break;
    case VE_A:
      bump_param(P_RHYTHM, d);
      break;
    case VE_B:
      bump_param(P_CHORD, d);
      break;
    default: break;
    }
    break;
  }

  case VIBE_EV_MIDI: {
    uint8_t st = e->midi[0] & 0xf0;
    if (st == 0xb0) {
      uint8_t cc = e->midi[1];
      float v = (float)e->midi[2] / 127.0f;
      if (cc == 7) set_param(P_MASTER, v * 2.0f, 1);
      else if (cc == 11) set_param(P_SWELL, v * 1.5f, 1);
      else if (cc == 62) toggle_param(P_ROTARY);
    }
    break;
  }

  case VIBE_EV_PEDAL:
    if (e->id == VIBE_PEDAL_EXPR) set_param(P_SWELL, e->value * 1.5f, 1);
    else if (e->id == VIBE_PEDAL_SWITCH && e->down) toggle_run();
    break;

  case VIBE_EV_TRANSPORT:
    /* A DAW's PLAY only supplies tempo/beat sync for drums the player has
     * already started. It never switches the drum bus on. */
    if (e->id == VIBE_TRANSPORT_TEMPO && e->value > 0.0f)
      set_param(P_BPM, e->value, 1);
    break;

  case VIBE_EV_SAVE:
    vibe_store_write("b3-state", g_p, (int)sizeof(g_p));
    break;

  default:
    break;
  }
}

void vibe_on_audio(const float *in, float *out_l, float *out_r, int frames)
{
  float drums_l[VIBE_MAX_BLOCK];
  float drums_r[VIBE_MAX_BLOCK];
  int i;
  (void)in;

  if (!g_inst.synth) {
    for (i = 0; i < frames; i++) { out_l[i] = 0; out_r[i] = 0; }
    return;
  }
  if (frames > VIBE_MAX_BLOCK) frames = VIBE_MAX_BLOCK;

  process_commands();
  release_stabs();
  update_sequencer(frames);
  b3_drums_render(drums_l, drums_r, frames, &g_audio_drums);

  for (i = 0; i < frames; i++) {
    float l, r;
    if (g_buf_off >= BUFFER_SIZE_SAMPLES) {
      render_engine_block();
      g_buf_off = 0;
    }
    l = g_bufL0[g_buf_off] * g_audio_master + drums_l[i];
    r = g_bufL1[g_buf_off] * g_audio_master + drums_r[i];
    g_buf_off++;
    if (!(l > -1e30f && l < 1e30f)) l = 0.0f;
    if (!(r > -1e30f && r < 1e30f)) r = 0.0f;
    /* gentle safety limiter: no hard clipping, even if a preset is pushed hot */
    out_l[i] = vd_clamp(0.95f * vd_tanh(1.1f * l), -0.97f, 0.97f);
    out_r[i] = vd_clamp(0.95f * vd_tanh(1.1f * r), -0.97f, 0.97f);
  }
  g_audio_samples += (uint64_t)frames;
}

void vibe_on_draw(uint16_t *fb)
{
  int i;
  vg_begin(fb);
  g_draw_tick++;

  if (g_help) {
    vg_clear(COL_BG);
    vg_controls("B3 CONTROLS", HELP_ROWS,
                (int)(sizeof(HELP_ROWS) / sizeof(HELP_ROWS[0]) / 2));
    return;
  }

  draw_focus_frame();

  for (i = 0; i < 6; i++)
    vibe_led_button(VB_F1 + i, i == g_page ? COL_PURPLE : COL_DIM,
                    i == g_page ? 1.0f : 0.22f);

  {
    int pat = (int)g_p[P_RHYTHM];
    int chord = (int)g_p[P_CHORD];
    int playing = g_p[P_RUN] >= 0.5f;
    int step = (int)__atomic_load_n(&g_audio_step, __ATOMIC_RELAXED) & 15;
    for (i = 0; i < 8; i++) {
      int on = (i == pat);
      float level = playing ? (on ? (step % 4 == 0 ? 1.0f : 0.72f) : 0.18f) : 0.30f;
      vibe_led_pad(i, on ? COL_PURPLE : COL_LINE, level);
      level = (i == chord) ? 0.95f : 0.20f;
      vibe_led_pad(8 + i, (i == chord) ? COL_LILAC : COL_LINE, level);
    }
  }

  softbar();
}
