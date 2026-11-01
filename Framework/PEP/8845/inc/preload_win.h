#pragma once

#include "driver.h"
#include "res_graph.h"

/* Publication holds the lifecycle mutex. These calls must precede OS device admission. */
NTSTATUS preload_win_start(res_graph_t*);
BOOLEAN preload_win_abort(res_graph_t*);
