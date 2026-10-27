#include "win_delay.h"
#include "power_delay.h"
#include <ntddk.h>

static int win_wait(void* ctx, uint32_t ms)
{
    (void)ctx;
    /* power transitions must already be at PASSIVE_LEVEL. */

    return NT_SUCCESS(power_delay_ms(ms));
}

void win_delay_init(delay_provider_t* provider)
{
    if (!provider)
        return;
    provider->ctx = NULL;
    provider->wait_ms = win_wait;
}
