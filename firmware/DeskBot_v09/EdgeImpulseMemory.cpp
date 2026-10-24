#include "EdgeImpulseMemory.h"

#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <cstdlib>
#include <cstring>

#include "Config.h"

namespace {
// ei_aligned_calloc() adds a uint16_t offset and up to 15 alignment bytes.
constexpr size_t ALIGNMENT_OVERHEAD_BYTES = sizeof(uint16_t) + 15;
constexpr size_t ARENA_BACKING_BYTES =
    config::VOICE_TENSOR_ARENA_BYTES + ALIGNMENT_OVERHEAD_BYTES;

void* tensorArenaBacking = nullptr;
bool tensorArenaLeased = false;
portMUX_TYPE allocatorLock = portMUX_INITIALIZER_UNLOCKED;

bool isTensorArenaRequest(size_t bytes) {
  return bytes >= config::VOICE_TENSOR_ARENA_MIN_REQUEST_BYTES &&
         bytes <= ARENA_BACKING_BYTES;
}
}  // namespace

bool reserveEdgeImpulseTensorArena() {
  portENTER_CRITICAL(&allocatorLock);
  const bool alreadyReserved = tensorArenaBacking != nullptr;
  portEXIT_CRITICAL(&allocatorLock);
  if (alreadyReserved) return true;

  void* allocated = heap_caps_calloc(
      1, ARENA_BACKING_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (allocated == nullptr) return false;

  portENTER_CRITICAL(&allocatorLock);
  if (tensorArenaBacking == nullptr) {
    tensorArenaBacking = allocated;
    allocated = nullptr;
  }
  portEXIT_CRITICAL(&allocatorLock);
  if (allocated != nullptr) heap_caps_free(allocated);
  return true;
}

bool edgeImpulseTensorArenaInPsram() {
  portENTER_CRITICAL(&allocatorLock);
  void* pointer = tensorArenaBacking;
  portEXIT_CRITICAL(&allocatorLock);
  return pointer != nullptr && esp_ptr_external_ram(pointer);
}

size_t edgeImpulseTensorArenaBytes() {
  portENTER_CRITICAL(&allocatorLock);
  const bool reserved = tensorArenaBacking != nullptr;
  portEXIT_CRITICAL(&allocatorLock);
  return reserved ? config::VOICE_TENSOR_ARENA_BYTES : 0;
}

// Edge Impulse's Arduino port declares these functions weak. Only the arena-
// sized allocation is retained; all smaller SDK allocations keep their normal
// calloc/free lifetime.
void* ei_calloc(size_t nitems, size_t size) {
  if (size != 0 && nitems > SIZE_MAX / size) return nullptr;
  const size_t bytes = nitems * size;
  if (!isTensorArenaRequest(bytes)) return calloc(nitems, size);
  if (!reserveEdgeImpulseTensorArena()) return nullptr;

  portENTER_CRITICAL(&allocatorLock);
  if (tensorArenaLeased) {
    portEXIT_CRITICAL(&allocatorLock);
    return calloc(nitems, size);
  }
  tensorArenaLeased = true;
  void* result = tensorArenaBacking;
  portEXIT_CRITICAL(&allocatorLock);
  memset(result, 0, bytes);
  return result;
}

void ei_free(void* pointer) {
  if (pointer == nullptr) return;
  portENTER_CRITICAL(&allocatorLock);
  if (pointer == tensorArenaBacking) {
    tensorArenaLeased = false;
    portEXIT_CRITICAL(&allocatorLock);
    return;
  }
  portEXIT_CRITICAL(&allocatorLock);
  free(pointer);
}
