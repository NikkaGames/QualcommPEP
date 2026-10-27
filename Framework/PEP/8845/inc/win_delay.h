#ifndef WIN_DELAY_H
#define WIN_DELAY_H
#include "delay_provider.h"
/* fill a caller-owned nonpaged provider instance. */

void win_delay_init(delay_provider_t* provider);
#endif
