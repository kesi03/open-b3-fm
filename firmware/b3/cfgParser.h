/* Minimal cfgParser declarations for the freestanding B3 firmware.
 * The engine's config-file parser is not used; the docs tables still compile.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef B3_CFGPARSER_H
#define B3_CFGPARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#define INCOMPLETE_DOC "", 0, 0, 0
#define DOC_SENTINEL { 0, CFG_TEXT, "", "", "", 0, 0, 0 }
#define DENORMAL_HACK (1e-14)
#define IS_DENORMAL(f) (((*(unsigned int *)&(f)) & 0x7f800000) == 0)

typedef struct _configContext {
    const char *fname;
    int linenr;
    const char *name;
    const char *value;
} ConfigContext;

enum conftype { CFG_TEXT = 0, CFG_DOUBLE, CFG_DECIBEL, CFG_FLOAT, CFG_INT, CFG_LAST };

typedef struct _configDoc {
    const char *name;
    enum conftype type;
    const char *dflt;
    const char *desc;
    const char *unit;
    const float min, max;
    const float ui_step;
} ConfigDoc;

int getConfigParameter_d(const char *par, ConfigContext *cfg, double *dp);
int getConfigParameter_i(const char *par, ConfigContext *cfg, int *dp);
int getConfigParameter_f(const char *par, ConfigContext *cfg, float *dp);
int getConfigParameter_ir(const char *par, ConfigContext *cfg, int *ip, int lowInc, int highInc);
int getConfigParameter_dr(const char *par, ConfigContext *cfg, double *dp, double lowInc, double highInc);
int getConfigParameter_fr(const char *par, ConfigContext *cfg, float *fp, float lowInc, float highInc);
const char *getConfigValue(ConfigContext *cfg);
void showConfigfileContext(ConfigContext *cfg, const char *msg);
void configDoubleUnparsable(ConfigContext *cfg);
void configIntOutOfRange(ConfigContext *cfg, int min, int max);

#ifdef __cplusplus
}
#endif
#endif
