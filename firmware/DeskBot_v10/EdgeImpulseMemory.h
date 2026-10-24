#pragma once

#include <Arduino.h>

// Reserves the model's arena backing store once in PSRAM. Edge Impulse's weak
// ei_calloc/ei_free hooks are overridden in the .cpp file so its aligned arena
// allocator leases this same block for every inference.
bool reserveEdgeImpulseTensorArena();
bool edgeImpulseTensorArenaInPsram();
size_t edgeImpulseTensorArenaBytes();
