#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Portable bounded receiver. Storage belongs to the caller; this module never
 * allocates, paints, or trusts a packet length supplied by the peer. */
typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t received;
    uint32_t id;
    uint32_t generation;
    bool verified;
} ble_photo_rx_t;

bool ble_photo_begin(ble_photo_rx_t *rx, uint8_t *buffer, size_t size, uint32_t id,
                     uint32_t generation);
bool ble_photo_append(ble_photo_rx_t *rx, const uint8_t *packet, size_t length,
                      size_t max_chunk, uint32_t generation);
/* Valid future packet: discard its pixels and ACK the unchanged offset. */
bool ble_photo_is_gap(const ble_photo_rx_t *rx, const uint8_t *packet, size_t length,
                      size_t max_chunk, uint32_t generation);
bool ble_photo_finish(ble_photo_rx_t *rx, const uint8_t expected[32], const uint8_t actual[32],
                      uint32_t generation);
void ble_photo_cancel(ble_photo_rx_t *rx);
