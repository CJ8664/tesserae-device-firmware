# E1003 setup memory investigation

Based on v1.47.0. Two independent failures were observed on a reTerminal
E1003: `ble_switch_btn` stack overflow during the captive-portal hold, and
`BLE_INIT: Malloc failed` followed by a controller assertion when entering
maintenance directly at boot. The portal splash also reported `OOM row scratch`.
PSRAM initialization and its memory test succeeded.

## Changes

- The 2 KB button task only detects the hold and signals the event. The main
  task logs the request and the button task's stack low-watermark.
- The E1003 enables ESP-IDF external BSS placement. Its 32,769-byte REST
  response buffer and two 19,284-byte touch specifications are CPU-only data
  and explicitly use `EXT_RAM_BSS_ATTR`. IDF additionally relocates supported
  networking-library BSS. Other targets retain their existing memory placement.
- DMA buffers, task stack sizes and Wi-Fi buffer counts remain unchanged.
  The previous E1004 fix (paint before starting BLE, serialized host teardown)
  remains in place.

## Bench build

Use a clean, whitespace-free build directory:

```sh
pio run -e seeed-reterminal-e1003-memorydiag
```

This inherits the existing soak target's `99.0.0-bench` identity and disables
OTA capability so the server cannot overwrite the test build during validation.
It must not be added to release matrices. The ordinary E1003 target also gets
the memory placement fix but has no extra memory logging.

When changing SDK defaults, regenerate the **bench copy's** generated
`sdkconfig.seeed-reterminal-e1003-memorydiag`; existing generated configs override
defaults. Never delete configs or local work in another checkout.

`mem_diag` records internal/DMA free bytes, the largest contiguous DMA block,
PSRAM free bytes and the calling task's stack low-watermark. Total free heap
alone is misleading on this device because it includes several MB of PSRAM.

## Validation scope

Before flashing, back up and verify the active image, configuration, partition
table and crash partition. Determine the active OTA slot from the boot log and
OTA selector; do not assume its offset on another device. Flash the app only,
using the E1003's ROM loader (`--no-stub --no-compress`).

Verify normal boot/network/display operation, a maintenance hold, BLE
advertising, Companion discovery/authentication and clean exit. For a board
already configured, normal maintenance does not exercise the captive-portal
button task; report that distinction instead of claiming both entry paths passed.

Local host tests cover the BLE protocol, maintenance gestures, log capture and
touch-v3 parsing. E1004 compilation is a compatibility check, not a physical
E1004 test.

## Other-board validation requested

Only E1003 hardware was available for validating this patch. External BSS is
enabled only for E1003; other targets retain their existing memory placement.
The button-task logging fix is shared by targets with BLE setup enabled.

Maintainer testing on other models would help confirm the shared portal-to-BLE
handoff, discovery, connection and clean exit. Please also check normal display
refresh and sleep/wake operation. Enabling external BSS on another PSRAM board
would be a separate, board-specific change requiring its own validation; this
patch does not claim that the E1003 memory savings apply to those models.

## Local results, 2026-10-14

- E1003 diagnostic and standard builds passed; E1004 standard build passed.
- BLE protocol, maintenance-button and log-capture host tests passed. Touch-v3
  tests passed all 256 checks.
- In the diagnostic build, static internal RAM fell from 181,612 to 96,172
  bytes (85,440 bytes freed). The linker places the three application buffers
  in external memory; IDF's supported networking BSS accounts for the rest.
- A connected E1003 accepted the app-only write and verified its hash. Saved
  Wi-Fi and server registration remained usable. It downloaded and painted a
  1,314,144-byte frame, then entered maintenance after a 4.3-second measured hold.
- After the BLE host started: 36,671 bytes of DMA heap free, with a 31,744-byte
  largest block. Phone connection completed with a 5,096-byte host-stack
  low-watermark; the firmware received `diagnostics` and `scan` commands.
- Reconnection also succeeded (host stack low-watermark 4,360 bytes). A phone
  `reboot` command produced `BLE host stopped cleanly`; the device restarted,
  reconnected to its saved Wi-Fi, downloaded and painted its dashboard frame
  again, then returned to its normal 60-second deep-sleep cycle. No new OOM,
  stack-overflow or panic appeared during this captured sequence.
- This configured device entered maintenance from a button wake, not from the
  captive portal. The portal handoff's relocated log still needs a fresh-device
  hardware regression; E1004 was only compile-checked in this run.
