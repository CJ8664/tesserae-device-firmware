#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* One atomically committed NVS blob. Read-only absent config is Automatic.
 * The caller supplies entropy for a newly authorized key; this module never
 * emits a key before commit succeeds. Clear Wi-Fi does not touch this namespace.
 * v2 adds a one-time prompt bit; existing v1 Manual state has no pending prompt.
 * Selecting Automatic blindly replaces even unreadable/corrupt old state. */
esp_err_t ble_photo_mode_get(bool *manual, uint8_t key[32]);
esp_err_t ble_photo_mode_set(bool manual, const uint8_t candidate[32], uint8_t authorized[32]);

/* Atomically consume the entry prompt BEFORE painting anything. show is true
 * only after a pending bit was cleared and committed. On failure show is false;
 * the caller must leave the panel untouched and not accept a photo yet. A reset
 * after commit but before paint may lose the prompt, never repeat it. */
esp_err_t ble_photo_mode_take_prompt(bool *show);

/* After a mode commit, disconnect is a pending reboot. Other state-changing
 * commands must not ACK an action that this disconnect could supersede. */
bool ble_photo_mode_pending_allows(const char *operation);
