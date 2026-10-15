#pragma once

/* Opt-in bench diagnostics: totals including PSRAM hide internal/DMA OOMs.
 * Call from an adequately sized task, never from the button polling task. */
#ifdef TESSERAE_MEMORY_DIAGNOSTICS
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline void memory_diag(const char *stage)
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    ESP_LOGI("mem_diag", "%s: dma_free=%u dma_largest=%u internal_free=%u "
             "psram_free=%u stack_min=%u", stage,
             (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
#else
#define memory_diag(stage) ((void)0)
#endif
