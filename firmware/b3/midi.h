/* Minimal MIDI-control declarations for the freestanding B3 firmware.
 * The engine registers callbacks here; the firmware calls the setters directly.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef B3_MIDI_H
#define B3_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void useMIDIControlFunction(void *m, const char *cfname, void (*f)(void *, unsigned char), void *d);
void notifyControlChangeByName(void *mcfg, const char *cfname, unsigned char val);
void notifyControlChangeById(void *mcfg, int id, unsigned char val);
void midi_panic(void *inst);

#ifdef __cplusplus
}
#endif
#endif
