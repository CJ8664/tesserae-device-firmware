#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ble_photo.h"

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
int main(void)
{
    ble_photo_rx_t rx = {0};
    uint8_t buffer[8], packet[16], sha[32] = {1}, bad[32] = {2};
    be32(packet, 5); be32(packet + 4, 0);
    memset(packet + 8, 0x1b, 8);
    assert(!ble_photo_begin(&rx, buffer, sizeof buffer, 0, 1));
    assert(!ble_photo_begin(&rx, buffer, sizeof buffer, 5, 0));
    assert(!ble_photo_begin(&rx, buffer, 4U * 1024U * 1024U + 1, 5, 1));
    assert(ble_photo_begin(&rx, buffer, sizeof buffer, 5, 1));
    assert(!ble_photo_append(&rx, packet, 8, 8, 1)); /* header without pixels */
    assert(!ble_photo_append(&rx, packet, 17, 8, 1)); /* oversized logical chunk */
    assert(!ble_photo_append(&rx, packet, 12, 8, 2)); /* stale generation */
    be32(packet + 4, 1);
    assert(!ble_photo_append(&rx, packet, 12, 8, 1)); /* future offset */
    assert(ble_photo_is_gap(&rx, packet, 12, 8, 1));
    assert(!ble_photo_is_gap(&rx, packet, 12, 8, 2)); /* stale session */
    assert(!ble_photo_is_gap(&rx, packet, 17, 8, 1)); /* oversized */
    assert(rx.received == 0);
    be32(packet + 4, 0); be32(packet, 6);
    assert(!ble_photo_append(&rx, packet, 12, 8, 1)); /* different transfer */
    be32(packet, 5);
    assert(ble_photo_append(&rx, packet, 12, 8, 1));
    assert(rx.received == 4);
    assert(ble_photo_append(&rx, packet, 12, 8, 1)); /* identical retry */
    assert(rx.received == 4);
    packet[8] ^= 1;
    assert(!ble_photo_append(&rx, packet, 12, 8, 1)); /* conflicting retry */
    packet[8] ^= 1;
    assert(!ble_photo_finish(&rx, sha, sha, 1)); /* incomplete */
    be32(packet + 4, UINT32_MAX);
    assert(!ble_photo_is_gap(&rx, packet, 12, 8, 1)); /* not a valid gap */
    assert(!ble_photo_append(&rx, packet, 12, 8, 1));
    be32(packet + 4, 4);
    assert(!ble_photo_append(&rx, packet, 16, 8, 1)); /* overflow */
    assert(ble_photo_append(&rx, packet, 12, 8, 1));
    assert(!ble_photo_finish(&rx, sha, bad, 1)); /* checksum rejected */
    assert(!rx.verified);
    assert(!ble_photo_finish(&rx, sha, sha, 2));
    assert(ble_photo_finish(&rx, sha, sha, 1));
    assert(rx.verified);
    assert(!ble_photo_append(&rx, packet, 12, 8, 1)); /* immutable commit */
    assert(!ble_photo_finish(&rx, sha, bad, 1));
    assert(rx.verified); /* a duplicate end cannot revoke a committed frame */
    ble_photo_cancel(&rx);
    assert(!rx.buffer && !rx.id && !rx.generation && !rx.received && !rx.verified);
    assert(!ble_photo_append(&rx, packet, 12, 8, 1));

    /* Four ordered packets can be queued before the client handles any ACK.
     * With every ACK lost, retrying just the oldest identical packet returns
     * rx.received for the entire window, without appending or corrupting data. */
    uint8_t window_buffer[14], window_packet[12];
    assert(ble_photo_begin(&rx, window_buffer, sizeof window_buffer, 9, 4));
    be32(window_packet, 9);
    for (size_t start = 0; start < sizeof window_buffer; start += 4) {
        size_t n = sizeof window_buffer - start;
        if (n > 4) n = 4;
        be32(window_packet + 4, (uint32_t)start);
        for (size_t i = 0; i < n; i++) window_packet[8 + i] = (uint8_t)(start + i);
        assert(ble_photo_append(&rx, window_packet, n + 8, 4, 4));
    }
    assert(rx.received == sizeof window_buffer);
    be32(window_packet + 4, 0);
    for (size_t i = 0; i < 4; i++) window_packet[8 + i] = (uint8_t)i;
    assert(ble_photo_append(&rx, window_packet, sizeof window_packet, 4, 4));
    assert(rx.received == sizeof window_buffer);
    for (size_t i = 0; i < sizeof window_buffer; i++) assert(window_buffer[i] == i);
    assert(ble_photo_finish(&rx, sha, sha, 4));
    ble_photo_cancel(&rx);

    /* MTU 256 permits 220 pixel bytes after 33 bytes of encrypted framing.
     * A dropped Write Command must not copy later data across the gap. After
     * the status barrier reports 220, resend from there with variable sizes. */
    uint8_t dynamic_buffer[731], dynamic_packet[228];
    memset(dynamic_buffer, 0xee, sizeof dynamic_buffer);
    assert(ble_photo_begin(&rx, dynamic_buffer, sizeof dynamic_buffer, 11, 5));
    be32(dynamic_packet, 11); be32(dynamic_packet + 4, 0);
    memset(dynamic_packet + 8, 0x31, 220);
    assert(ble_photo_append(&rx, dynamic_packet, sizeof dynamic_packet, 384, 5));
    be32(dynamic_packet + 4, 440); /* packet at 220 was lost */
    assert(ble_photo_is_gap(&rx, dynamic_packet, sizeof dynamic_packet, 384, 5));
    assert(!ble_photo_append(&rx, dynamic_packet, sizeof dynamic_packet, 384, 5));
    assert(rx.received == 220 && dynamic_buffer[440] == 0xee);
    while (rx.received < sizeof dynamic_buffer) {
        size_t n = sizeof dynamic_buffer - rx.received;
        if (n > 220) n = 220;
        be32(dynamic_packet + 4, (uint32_t)rx.received);
        assert(ble_photo_append(&rx, dynamic_packet, 8 + n, 384, 5));
    }
    be32(dynamic_packet + 4, 220);
    assert(ble_photo_append(&rx, dynamic_packet, sizeof dynamic_packet, 384, 5));
    dynamic_packet[8] ^= 1;
    assert(!ble_photo_append(&rx, dynamic_packet, sizeof dynamic_packet, 384, 5));
    assert(!ble_photo_is_gap(&rx, dynamic_packet, sizeof dynamic_packet, 384, 5));
    for (size_t i = 0; i < sizeof dynamic_buffer; i++) assert(dynamic_buffer[i] == 0x31);
    assert(ble_photo_finish(&rx, sha, sha, 5));
    assert(!ble_photo_is_gap(&rx, dynamic_packet, sizeof dynamic_packet, 384, 5));
    ble_photo_cancel(&rx);

    /* Maximum supported frame, including the final short chunk.
     * The session layer computes actual SHA-256; this portable core verifies
     * the independently computed expected/actual hashes it is given. */
    const size_t size = 4U * 1024U * 1024U;
    uint8_t *large = malloc(size);
    assert(large);
    assert(ble_photo_begin(&rx, large, size, 0x01020304, 3));
    uint8_t block[392];
    be32(block, 0x01020304);
    size_t offset = 0;
    while (offset < size) {
        size_t n = size - offset < 384 ? size - offset : 384;
        be32(block + 4, (uint32_t)offset);
        for (size_t i = 0; i < n; i++) block[8 + i] = (uint8_t)((offset + i) * 31);
        assert(ble_photo_append(&rx, block, n + 8, 384, 3));
        offset += n;
    }
    for (size_t i = 0; i < size; i++) assert(large[i] == (uint8_t)(i * 31));
    assert(ble_photo_finish(&rx, sha, sha, 3));
    free(large);
    ble_photo_cancel(&rx);
    puts("BLE photo receiver: bounds/retry/checksum/cancel/generation/4MiB PASS");
    return 0;
}
