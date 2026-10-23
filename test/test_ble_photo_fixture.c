#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ble_photo.h"
#include "mbedtls/sha256.h"

static void be32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
int main(int argc, char **argv)
{
    assert(argc == 3 && strlen(argv[2]) == 64);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long length = ftell(f); assert(length > 0 && length <= 4*1024*1024);
    rewind(f);
    uint8_t *received = malloc((size_t)length); assert(received);
    ble_photo_rx_t rx = {0};
    assert(ble_photo_begin(&rx, received, (size_t)length, 0x01020304, 10));
    uint8_t packet[392], expected[32], actual[32];
    be32(packet, 0x01020304);
    for (size_t offset = 0; offset < (size_t)length;) {
        be32(packet + 4, (uint32_t)offset);
        size_t n = fread(packet + 8, 1, 384, f); assert(n > 0);
        assert(ble_photo_append(&rx, packet, n + 8, 384, 10));
        offset += n;
    }
    fclose(f);
    for (size_t i = 0; i < 32; i++) {
        unsigned v; assert(sscanf(argv[2] + i*2, "%2x", &v) == 1); expected[i] = (uint8_t)v;
    }
    assert(mbedtls_sha256(received, (size_t)length, actual, 0) == 0);
    assert(ble_photo_finish(&rx, expected, actual, 10));
    assert(rx.verified);
    free(received);
    printf("cross-platform Photo v2 fixture: %ld bytes PASS\n", length);
    return 0;
}
