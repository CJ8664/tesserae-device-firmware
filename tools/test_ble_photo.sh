#!/bin/sh
set -eu

# Resolve the ESP-IDF mbedTLS sources through the project's version-pinned
# platform before inspecting the package directory. This must work on CI's
# fresh host-test checkout, without an earlier firmware build or local cache.
photo_pio_bin="${PIO_BIN:-$HOME/.platformio/penv/bin/pio}"
"$photo_pio_bin" pkg install -e seeed-reterminal-e1004-ble-photo >/dev/null
photo_core_dir="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
mbedtls="$photo_core_dir/packages/framework-espidf/components/mbedtls/mbedtls"
if [ ! -d "$mbedtls/library" ]; then
    echo "ESP-IDF mbedTLS not found at $mbedtls" >&2
    exit 1
fi

out="$(mktemp -d -t tesserae-ble-photo.XXXXXX)"
trap 'rm -rf "$out"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src test/test_ble_photo.c src/ble_photo.c -o "$out/receiver"
"$out/receiver"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I test/ble_mode_stubs -I src test/test_ble_photo_mode.c src/ble_photo_mode.c -o "$out/mode"
"$out/mode"

cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DMBEDTLS_CONFIG_FILE='"relay_mbedtls_config.h"' \
    -I src -I test -I "$mbedtls/include" \
    test/test_ble_photo_fixture.c src/ble_photo.c \
    "$mbedtls/library/sha256.c" "$mbedtls/library/platform_util.c" \
    "$mbedtls/library/constant_time.c" -o "$out/fixture"
python3 tools/generate_ble_photo_fixtures.py "$out/fixtures"
for profile in e1004 e1001 sticky e1003; do
    cmp "$out/fixtures/$profile.capabilities.json" "test/fixtures/ble_photo/$profile.capabilities.json"
    cmp "$out/fixtures/$profile.sha256" "test/fixtures/ble_photo/$profile.sha256"
    "$out/fixture" "$out/fixtures/$profile.bin" "$(cat "$out/fixtures/$profile.sha256")"
done
if [ "$#" -eq 2 ]; then
    "$out/fixture" "$1" "$2"
elif [ "$#" -ne 0 ]; then
    echo "Usage: $0 [external-frame.bin sha256]" >&2
    exit 1
fi
