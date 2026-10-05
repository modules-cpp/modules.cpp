/* Pawel Wodnicki (C) 2026 */
/* 32bitmicro LLC (C) 2026 */
/* Project-owned glue over the foreign file, compiled with warnings as errors. */
#include "glue-c.h"

#include "cdemo.h"

int mm_cdemo_twice(int value) { return 2 * cdemo_scale(value); }
