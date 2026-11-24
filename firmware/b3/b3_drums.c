/* B3 firmware - synthesized ride/brush/kit voices and eight organ grooves.
 * Based on FUNKY-TRAIN's drums.c (Apache-2.0, keste), rewritten for the
 * Hammond-style rhythm pads. No samples, no allocation, no host calls.
 * SPDX-License-Identifier: AGPL-3.0-or-later (this firmware)
 */
#include "b3_drums.h"

static const char *const RHYTHM_NAMES[B3_RHYTHMS] = {
  "JAZZ RIDE", "SHUFFLE", "BOSSA", "FUNK",
  "GOSPEL", "ROCK", "LATIN", "BALLAD"
};

static const char *const VOICE_NAMES[B3_D_COUNT] = {
  "KICK", "SNARE", "RIM", "HAT", "OPEN", "RIDE", "TOM", "BRUSH"
};

/* bit 0 kick, 1 snare, 2 rim, 3 hat, 4 open, 5 ride, 6 tom, 7 brush */
static const unsigned char FACTORY[B3_RHYTHMS][B3_DRUM_STEPS] = {
  /* JAZZ RIDE: feathered 2/4, ride on the swing pattern, brushes on 2 and 4 */
  { 0x21,0x00,0x20,0x00, 0x82,0x00,0x20,0x00, 0x21,0x00,0x20,0x00, 0x82,0x00,0x20,0x80 },
  /* SHUFFLE: blues shuffle with a driving ride */
  { 0x21,0x00,0x28,0x00, 0x81,0x00,0x28,0x00, 0x21,0x00,0x28,0x00, 0x81,0x20,0x28,0x80 },
  /* BOSSA: soft cross-stick, closed hat, light ride */
  { 0x09,0x08,0x28,0x08, 0x04,0x08,0x28,0x08, 0x09,0x08,0x28,0x08, 0x04,0x08,0x28,0x08 },
  /* FUNK: syncopated kick/snare/hat */
  { 0x09,0x08,0x2A,0x08, 0x02,0x08,0x29,0x18, 0x09,0x08,0x2A,0x08, 0x02,0x48,0x29,0x18 },
  /* GOSPEL: big backbeat, tom fills */
  { 0x01,0x08,0x2C,0x08, 0x02,0x08,0x2C,0x48, 0x01,0x08,0x2C,0x08, 0x02,0x48,0x2C,0x48 },
  /* ROCK: straight eighths */
  { 0x01,0x08,0x09,0x08, 0x02,0x08,0x09,0x08, 0x01,0x08,0x09,0x08, 0x02,0x08,0x49,0x08 },
  /* LATIN: montuno-ish, rim and ride accents */
  { 0x09,0x08,0x24,0x08, 0x04,0x28,0x24,0x08, 0x09,0x08,0x24,0x08, 0x04,0x68,0x24,0x08 },
  /* BALLAD: sparse brushes and ride */
  { 0x01,0x00,0x80,0x00, 0x82,0x00,0x00,0x00, 0x01,0x00,0x80,0x00, 0x82,0x00,0x00,0x80 }
};

typedef struct {
  volatile int on, kind, age;
  int samples;
  float ph, ph2, env, dec, lp, hp_lp, bp, f0, f1, pan_l, pan_r, vel;
  vd_noise noise;
} b3_dvoice;

static float s_sr;
static b3_dvoice dv[B3_DRUM_VOICES];
static int age_ctr;

void b3_drums_init(float sr)
{
  int i;
  s_sr = sr > 1000.0f ? sr : 48000.0f;
  for (i = 0; i < B3_DRUM_VOICES; i++) { dv[i].on = 0; dv[i].samples = 0; }
  age_ctr = 0;
  VIBE_RELEASE();
}

void b3_drums_reset(void)
{
  int i;
  for (i = 0; i < B3_DRUM_VOICES; i++) dv[i].on = 0;
}

unsigned char b3_drum_step(int pat, int step)
{
  if (pat < 0 || pat >= B3_RHYTHMS) pat = 0;
  if (step < 0 || step >= B3_DRUM_STEPS) step = 0;
  return FACTORY[pat][step];
}

const char *b3_rhythm_name(int pat)
{
  if (pat < 0 || pat >= B3_RHYTHMS) pat = 0;
  return RHYTHM_NAMES[pat];
}

const char *b3_drum_voice_name(int voice)
{
  if (voice < 0 || voice >= B3_D_COUNT) voice = 0;
  return VOICE_NAMES[voice];
}

void b3_drums_trigger(int voice, int vel)
{
  b3_dvoice *v = 0;
  int i, best = -1, oldest = -1;
  float amp = vd_clamp((float)vel / 127.0f, 0.05f, 1.0f);

  if (voice < 0 || voice >= B3_D_COUNT) voice = B3_D_KICK;
  for (i = 0; i < B3_DRUM_VOICES; i++) {
    if (!dv[i].on) { best = i; break; }
    if (dv[i].age > oldest) { oldest = dv[i].age; best = i; }
  }
  if (best < 0) return;
  v = &dv[best];
  v->on = 1;
  v->kind = voice;
  v->age = ++age_ctr;
  v->samples = 0;
  v->env = 1.0f;
  v->vel = amp;
  v->ph = v->ph2 = v->lp = v->hp_lp = v->bp = 0.0f;
  v->noise.s = 0x9E3779B9u + (uint32_t)(age_ctr * 2654435761u);
  switch (voice) {
  case B3_D_KICK:  v->f0 = 130.f; v->f1 = 48.f;  v->dec = 0.30f; v->pan_l = v->pan_r = .74f; break;
  case B3_D_SNARE: v->f0 = 190.f; v->f1 = 150.f; v->dec = 0.16f; v->pan_l = .76f; v->pan_r = .68f; break;
  case B3_D_RIM:   v->f0 = 420.f; v->f1 = 250.f; v->dec = 0.07f; v->pan_l = .70f; v->pan_r = .74f; break;
  case B3_D_HAT:   v->f0 = 0.f;   v->f1 = 0.f;   v->dec = 0.045f; v->pan_l = .80f; v->pan_r = .62f; break;
  case B3_D_OPEN:  v->f0 = 0.f;   v->f1 = 0.f;   v->dec = 0.34f; v->pan_l = .60f; v->pan_r = .82f; break;
  case B3_D_RIDE:  v->f0 = 0.f;   v->f1 = 0.f;   v->dec = 0.55f; v->pan_l = .66f; v->pan_r = .78f; break;
  case B3_D_TOM:   v->f0 = 200.f; v->f1 = 95.f;  v->dec = 0.26f; v->pan_l = .70f; v->pan_r = .70f; break;
  default:         v->f0 = 0.f;   v->f1 = 0.f;   v->dec = 0.13f; v->pan_l = .72f; v->pan_r = .70f; break;
  }
}

void b3_drums_render(float *l, float *r, int n, const b3_drum_ctrl *c)
{
  int i, vi;
  float gain = c ? c->gain : 0.8f;
  float tune = c ? vd_clamp(c->tune, 0.5f, 1.8f) : 1.0f;
  float decs = c ? vd_clamp(c->decay, 0.25f, 2.5f) : 1.0f;
  float drive = c ? vd_clamp(c->drive, 0.0f, 1.0f) : 0.0f;
  float pan   = c ? vd_clamp(c->pan, -1.0f, 1.0f) : 0.0f;
  float pl    = 1.0f - 0.5f * pan;
  float pr    = 1.0f + 0.5f * pan;

  for (i = 0; i < n; i++) {
    float out_l = 0.0f, out_r = 0.0f;
    for (vi = 0; vi < B3_DRUM_VOICES; vi++) {
      b3_dvoice *v = &dv[vi];
      float s = 0.0f, env;
      if (!v->on) continue;
      v->samples++;
      env = v->env;
      if (env < 0.0007f) { v->on = 0; continue; }

      if (v->kind == B3_D_KICK || v->kind == B3_D_TOM) {
        float f = (v->f1 + (v->f0 - v->f1) * env * env) * tune;
        v->ph += vd_clamp(f, 25.f, 1000.f) / s_sr;
        if (v->ph >= 1.0f) v->ph -= 1.0f;
        s = vd_sin(v->ph) * env;
        if (v->kind == B3_D_KICK && v->samples < (int)(s_sr * 0.004f))
          s += vd_noise_next(&v->noise) * 0.32f * (1.0f - (float)v->samples / (s_sr * 0.004f));
      } else if (v->kind == B3_D_SNARE || v->kind == B3_D_RIM) {
        float nz = vd_noise_next(&v->noise);
        v->ph += vd_clamp(v->f1 * tune, 40.f, 1000.f) / s_sr;
        if (v->ph >= 1.0f) v->ph -= 1.0f;
        v->hp_lp += 0.34f * (nz - v->hp_lp);
        s = ((v->kind == B3_D_RIM ? 0.75f : 0.48f) * (nz - v->hp_lp) +
             0.42f * vd_sin(v->ph)) * env;
      } else if (v->kind == B3_D_RIDE) {
        float nz = vd_noise_next(&v->noise);
        v->ph += 0.0137f; v->ph2 += 0.0213f;
        if (v->ph >= 1.0f) v->ph -= 1.0f;
        if (v->ph2 >= 1.0f) v->ph2 -= 1.0f;
        v->bp += 0.21f * ((vd_sin(v->ph) + vd_sin(v->ph2) * 0.7f + nz * 0.8f) - v->bp);
        s = (nz * 0.35f + v->bp * 0.65f) * env * 0.42f;
      } else if (v->kind == B3_D_BRUSH) {
        float nz = vd_noise_next(&v->noise);
        v->lp += 0.075f * (nz - v->lp);
        v->ph += 0.0009f;
        if (v->ph >= 1.0f) v->ph -= 1.0f;
        s = (v->lp * 0.75f + vd_sin(v->ph) * 0.25f) * env * 0.7f;
      } else {
        float nz = vd_noise_next(&v->noise);
        v->hp_lp += (v->kind == B3_D_OPEN ? 0.25f : 0.42f) * (nz - v->hp_lp);
        s = (nz - v->hp_lp) * env * (v->kind == B3_D_OPEN ? 0.48f : 0.52f);
      }

      if (drive > 0.0f) s = vd_tanh(s * (1.0f + 4.0f * drive));
      s *= v->vel;
      out_l += s * v->pan_l;
      out_r += s * v->pan_r;
      v->env *= vd_coef(v->dec / decs / 5.0f, s_sr);
    }
    l[i] = out_l * gain * pl;
    r[i] = out_r * gain * pr;
  }
}
