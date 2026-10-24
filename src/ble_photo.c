#include "ble_photo.h"
#include <string.h>

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

bool ble_photo_begin(ble_photo_rx_t *rx, uint8_t *buffer, size_t size, uint32_t id,
                     uint32_t generation)
{
    if (!rx || !buffer || !size || size > 4U * 1024U * 1024U || !id || !generation) return false;
    *rx = (ble_photo_rx_t){.buffer = buffer, .size = size, .id = id, .generation = generation};
    return true;
}

static bool valid_packet(const ble_photo_rx_t *rx, const uint8_t *packet, size_t length,
                         size_t max_chunk, uint32_t generation)
{
    if (!rx || !rx->buffer || rx->verified || generation != rx->generation || !packet || length <= 8 ||
        length - 8 > max_chunk || read_be32(packet) != rx->id) return false;
    size_t offset = read_be32(packet + 4), n = length - 8;
    return offset <= rx->size && n <= rx->size - offset;
}

bool ble_photo_is_gap(const ble_photo_rx_t *rx, const uint8_t *packet, size_t length,
                      size_t max_chunk, uint32_t generation)
{
    return valid_packet(rx, packet, length, max_chunk, generation) &&
           read_be32(packet + 4) > rx->received;
}

bool ble_photo_append(ble_photo_rx_t *rx, const uint8_t *packet, size_t length,
                      size_t max_chunk, uint32_t generation)
{
    if (!valid_packet(rx, packet, length, max_chunk, generation)) return false;
    size_t offset = read_be32(packet + 4), n = length - 8;
    if (offset > rx->received) return false;
    if (offset < rx->received) {
        return n <= rx->received - offset && memcmp(rx->buffer + offset, packet + 8, n) == 0;
    }
    memcpy(rx->buffer + offset, packet + 8, n);
    rx->received += n;
    return true;
}

bool ble_photo_finish(ble_photo_rx_t *rx, const uint8_t expected[32], const uint8_t actual[32],
                      uint32_t generation)
{
    if (!rx || !rx->buffer || rx->verified || generation != rx->generation ||
        !expected || !actual || rx->received != rx->size) return false;
    unsigned difference = 0;
    for (size_t i = 0; i < 32; i++) difference |= expected[i] ^ actual[i];
    rx->verified = difference == 0;
    return rx->verified;
}

void ble_photo_cancel(ble_photo_rx_t *rx)
{
    if (rx) memset(rx, 0, sizeof *rx);
}
