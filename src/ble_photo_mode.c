#include "ble_photo_mode.h"
#include <stddef.h>
#include <string.h>
#include "nvs.h"

enum { LEGACY_CONFIG_SIZE = 34, CONFIG_SIZE = 35, CONFIG_VERSION = 2, PROMPT_OFFSET = 34 };

typedef struct {
    bool manual;
    bool prompt_pending;
    uint8_t key[32];
} mode_state_t;

bool ble_photo_mode_pending_allows(const char *operation)
{
    return operation &&
        (strcmp(operation, "diagnostics") == 0 ||
         strcmp(operation, "set_screen_mode") == 0 ||
         strcmp(operation, "reboot") == 0);
}

static esp_err_t read_state(mode_state_t *state)
{
    memset(state, 0, sizeof *state);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_photo", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    uint8_t config[CONFIG_SIZE] = {0};
    size_t length = sizeof config;
    err = nvs_get_blob(handle, "config", config, &length);
    nvs_close(handle);
    if (err != ESP_OK) {
        memset(config, 0, sizeof config);
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    bool legacy = length == LEGACY_CONFIG_SIZE && config[0] == 1;
    bool current = length == CONFIG_SIZE && config[0] == CONFIG_VERSION;
    unsigned nonzero = 0;
    for (size_t i = 2; i < LEGACY_CONFIG_SIZE; i++) nonzero |= config[i];
    if ((!legacy && !current) || config[1] > 1 ||
        (current && (config[PROMPT_OFFSET] > 1 ||
                     (config[1] == 0 && config[PROMPT_OFFSET] != 0))) ||
        (config[1] == 1 && nonzero == 0) || (config[1] == 0 && nonzero != 0)) {
        memset(config, 0, sizeof config);
        return ESP_ERR_INVALID_STATE;
    }
    state->manual = config[1] == 1;
    /* A v1 Manual device may already display a photo. An upgrade must not
     * create a fresh entry prompt; its next successful write migrates to v2. */
    state->prompt_pending = current && config[PROMPT_OFFSET] == 1;
    if (state->manual) memcpy(state->key, config + 2, sizeof state->key);
    memset(config, 0, sizeof config);
    return ESP_OK;
}

static esp_err_t write_state(const mode_state_t *state)
{
    uint8_t config[CONFIG_SIZE] = {CONFIG_VERSION, state->manual ? 1 : 0};
    if (state->manual) memcpy(config + 2, state->key, sizeof state->key);
    config[PROMPT_OFFSET] = state->prompt_pending ? 1 : 0;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_photo", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "config", config, sizeof config);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    memset(config, 0, sizeof config);
    return err;
}

esp_err_t ble_photo_mode_get(bool *manual, uint8_t key[32])
{
    if (!manual || !key) return ESP_ERR_INVALID_ARG;
    *manual = false;
    memset(key, 0, 32);
    mode_state_t state;
    esp_err_t err = read_state(&state);
    if (err == ESP_OK) {
        *manual = state.manual;
        if (state.manual) memcpy(key, state.key, sizeof state.key);
    }
    memset(&state, 0, sizeof state);
    return err;
}

esp_err_t ble_photo_mode_set(bool manual, const uint8_t candidate[32], uint8_t authorized[32])
{
    if (!authorized) return ESP_ERR_INVALID_ARG;
    memset(authorized, 0, 32);
    if (manual && !candidate) return ESP_ERR_INVALID_ARG;
    mode_state_t state = {0};
    esp_err_t err = ESP_OK;
    if (manual) {
        err = read_state(&state);
        if (err == ESP_OK && !state.manual) {
            state.manual = true;
            state.prompt_pending = true;
            memcpy(state.key, candidate, sizeof state.key);
        }
        unsigned nonzero = 0;
        for (size_t i = 0; i < sizeof state.key; i++) nonzero |= state.key[i];
        if (err == ESP_OK && nonzero == 0) err = ESP_ERR_INVALID_ARG;
    }
    /* Explicit Automatic is a recovery operation: no read, key reuse or
     * migration is needed. Replace mode/key/prompt together with safe zeros. */
    if (err == ESP_OK) err = write_state(&state);
    if (err == ESP_OK && manual) memcpy(authorized, state.key, sizeof state.key);
    memset(&state, 0, sizeof state);
    return err;
}

esp_err_t ble_photo_mode_take_prompt(bool *show)
{
    if (!show) return ESP_ERR_INVALID_ARG;
    *show = false;
    mode_state_t state;
    esp_err_t err = read_state(&state);
    if (err == ESP_OK && state.manual && state.prompt_pending) {
        state.prompt_pending = false;
        err = write_state(&state);
        if (err == ESP_OK) *show = true;
    }
    memset(&state, 0, sizeof state);
    return err;
}
