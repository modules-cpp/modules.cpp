#include "cdemo.h"

/* CDEMO_SCALE comes from a c-option, CDEMO_OFFSET from the forced config.h. */
int cdemo_scale(int value) { return value * CDEMO_SCALE + CDEMO_OFFSET; }
