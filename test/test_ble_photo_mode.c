#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ble_photo_mode.h"
#include "nvs.h"

static uint8_t durable[64], pending[64];
static size_t durable_len, pending_len;
static bool namespace_present, fail_commit;
static esp_err_t get_error, readonly_open_error, write_open_error, set_error;
static unsigned reads, writes, commits;

esp_err_t nvs_open(const char *space, int mode, nvs_handle_t *handle)
{
    assert(strcmp(space, "ble_photo") == 0);
    if (mode == NVS_READONLY) {
        reads++;
        if (readonly_open_error) return readonly_open_error;
        if (!namespace_present) return ESP_ERR_NVS_NOT_FOUND;
    } else if (write_open_error) return write_open_error;
    *handle = (nvs_handle_t)(mode + 1);
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *length)
{
    assert(h == 1 && strcmp(key, "config") == 0);
    if (get_error) return get_error;
    if (!durable_len) return ESP_ERR_NVS_NOT_FOUND;
    if (*length < durable_len) return ESP_ERR_NVS_INVALID_LENGTH;
    *length = durable_len;
    memcpy(out, durable, durable_len);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length)
{
    assert(h == 2 && strcmp(key, "config") == 0 && length == 35);
    writes++;
    if (set_error) return set_error;
    memcpy(pending, value, length);
    pending_len = length;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    assert(h == 2 && pending_len == 35);
    commits++;
    if (fail_commit) return ESP_FAIL;
    memcpy(durable, pending, pending_len);
    durable_len = pending_len;
    namespace_present = true;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h)
{
    if (h == 2) {
        memset(pending, 0, sizeof pending);
        pending_len = 0;
    }
}
static bool zeros(const uint8_t key[32])
{
    unsigned value = 0;
    for (size_t i = 0; i < 32; i++) value |= key[i];
    return value == 0;
}
static void reset_storage(void)
{
    memset(durable, 0, sizeof durable);
    memset(pending, 0, sizeof pending);
    durable_len = pending_len = 0;
    namespace_present = fail_commit = false;
    get_error = readonly_open_error = write_open_error = set_error = ESP_OK;
    reads = writes = commits = 0;
}
static void install_legacy(bool manual, const uint8_t key[32])
{
    reset_storage();
    namespace_present = true;
    durable_len = 34;
    durable[0] = 1;
    durable[1] = manual;
    if (manual) memcpy(durable + 2, key, 32);
}
static void assert_mode(bool expected, const uint8_t expected_key[32])
{
    bool manual = !expected;
    uint8_t key[32];
    memset(key, 0x77, sizeof key);
    assert(ble_photo_mode_get(&manual, key) == ESP_OK && manual == expected);
    assert(expected ? memcmp(key, expected_key, 32) == 0 : zeros(key));
}
static void assert_no_prompt(void)
{
    bool show = true;
    unsigned before = writes;
    assert(ble_photo_mode_take_prompt(&show) == ESP_OK && !show);
    assert(writes == before); /* no flash wear on ordinary retained-photo wakes */
}
static void assert_read_fails_closed(esp_err_t expected, const uint8_t candidate[32])
{
    bool manual = true, show = true;
    uint8_t key[32];
    memset(key, 0x77, sizeof key);
    unsigned before = writes;
    assert(ble_photo_mode_get(&manual, key) == expected && !manual && zeros(key));
    assert(ble_photo_mode_set(true, candidate, key) == expected && zeros(key));
    assert(ble_photo_mode_take_prompt(&show) == expected && !show);
    assert(writes == before);
}
int main(void)
{
    uint8_t key[32], first[32], second[32], zero[32] = {0};
    memset(first, 1, sizeof first); memset(second, 2, sizeof second);
    reset_storage();
    assert_mode(false, NULL);
    assert_no_prompt();
    assert(ble_photo_mode_take_prompt(NULL) == ESP_ERR_INVALID_ARG);
    assert(ble_photo_mode_set(true, NULL, key) == ESP_ERR_INVALID_ARG && zeros(key));
    assert(ble_photo_mode_set(true, zero, key) == ESP_ERR_INVALID_ARG && zeros(key));

    /* Mode, key and first-entry prompt are one committed value. */
    fail_commit = true;
    assert(ble_photo_mode_set(true, first, key) == ESP_FAIL && zeros(key));
    assert_mode(false, NULL);
    fail_commit = false;
    assert(ble_photo_mode_set(true, first, key) == ESP_OK && memcmp(key, first, 32) == 0);
    assert(durable_len == 35 && durable[0] == 2 && durable[1] == 1 && durable[34] == 1);
    assert_mode(true, first);
    assert(ble_photo_mode_set(true, second, key) == ESP_OK && memcmp(key, first, 32) == 0);
    assert(durable[34] == 1); /* reauthorization preserves an unconsumed prompt */

    /* Prompt consumption may authorize paint only after successful commit. */
    bool show = true;
    write_open_error = ESP_FAIL;
    assert(ble_photo_mode_take_prompt(&show) == ESP_FAIL && !show && durable[34] == 1);
    write_open_error = ESP_OK;
    set_error = ESP_FAIL;
    assert(ble_photo_mode_take_prompt(&show) == ESP_FAIL && !show && durable[34] == 1);
    set_error = ESP_OK;
    fail_commit = true;
    show = true;
    assert(ble_photo_mode_take_prompt(&show) == ESP_FAIL && !show && durable[34] == 1);
    fail_commit = false;
    unsigned before_commits = commits;
    assert(ble_photo_mode_take_prompt(&show) == ESP_OK && show);
    assert(commits == before_commits + 1 && durable[34] == 0);
    assert_mode(true, first);
    /* Reset before paint, later photo boot and battery recovery all reread
     * persistent state; none may reissue the prompt after successful consume. */
    assert_no_prompt();
    assert_no_prompt();
    assert(ble_photo_mode_set(true, second, key) == ESP_OK && memcmp(key, first, 32) == 0);
    assert_no_prompt(); /* repeated authorization must not re-arm consumption */

    fail_commit = true;
    assert(ble_photo_mode_set(false, NULL, key) == ESP_FAIL && zeros(key));
    assert_mode(true, first);
    fail_commit = false;
    assert(ble_photo_mode_set(false, NULL, key) == ESP_OK && zeros(key));
    assert_mode(false, NULL);
    assert_no_prompt();
    assert(durable[34] == 0 && zeros(durable + 2));
    assert(ble_photo_mode_set(true, second, key) == ESP_OK && memcmp(key, second, 32) == 0);
    assert(durable[34] == 1); /* a new Automatic->Manual entry re-arms once */
    assert(ble_photo_mode_set(false, NULL, key) == ESP_OK && durable[34] == 0);

    /* v1 Manual may already display a photo. Read-only compatibility does not
     * invent a prompt; a successful mutation migrates key/state in one write. */
    install_legacy(true, first);
    assert_mode(true, first);
    assert_no_prompt();
    assert(durable_len == 34 && writes == 0);
    fail_commit = true;
    assert(ble_photo_mode_set(true, second, key) == ESP_FAIL && zeros(key));
    assert(durable_len == 34 && durable[0] == 1);
    fail_commit = false;
    assert(ble_photo_mode_set(true, second, key) == ESP_OK && memcmp(key, first, 32) == 0);
    assert(durable_len == 35 && durable[0] == 2 && durable[34] == 0);
    assert_no_prompt();
    install_legacy(false, NULL);
    assert(ble_photo_mode_set(true, second, key) == ESP_OK && durable[34] == 1);
    assert_mode(true, second);

    /* Automatic must blind-write recovery regardless of old version, length,
     * values or read errors; Manual and prompt consumption remain fail-closed. */
    for (unsigned scenario = 0; scenario < 8; scenario++) {
        reset_storage();
        assert(ble_photo_mode_set(true, first, key) == ESP_OK);
        esp_err_t expected = ESP_ERR_INVALID_STATE;
        switch (scenario) {
        case 0: durable[0] = 99; break;
        case 1: durable_len = 33; break;
        case 2: durable_len = 50; expected = ESP_ERR_NVS_INVALID_LENGTH; break;
        case 3: durable[34] = 2; break;
        case 4: durable[1] = 0; break;
        case 5: memset(durable + 2, 0, 32); break;
        case 6: get_error = ESP_FAIL; expected = ESP_FAIL; break;
        case 7: readonly_open_error = ESP_FAIL; expected = ESP_FAIL; break;
        }
        assert_read_fails_closed(expected, second);
        unsigned before_reads = reads;
        assert(ble_photo_mode_set(false, NULL, key) == ESP_OK && zeros(key));
        assert(reads == before_reads); /* no read dependency in recovery path */
        assert(durable_len == 35 && durable[0] == 2 && durable[1] == 0 &&
               zeros(durable + 2) && durable[34] == 0);
        get_error = readonly_open_error = ESP_OK;
        assert_mode(false, NULL);
        assert_no_prompt();
    }
    reset_storage();
    readonly_open_error = ESP_FAIL;
    write_open_error = ESP_FAIL;
    assert(ble_photo_mode_set(false, NULL, key) == ESP_FAIL && zeros(key));
    write_open_error = ESP_OK;
    set_error = ESP_FAIL;
    assert(ble_photo_mode_set(false, NULL, key) == ESP_FAIL && zeros(key));
    assert(!namespace_present); /* failed recovery must not report success */

    assert(ble_photo_mode_pending_allows("diagnostics"));
    assert(ble_photo_mode_pending_allows("set_screen_mode"));
    assert(ble_photo_mode_pending_allows("reboot"));
    const char *blocked[] = {"clear_wifi", "factory_reset", "stage", "apply", "scan",
        "photo_begin", "photo_capabilities", "diagnostics_extra", "reboot_now", "", NULL};
    for (size_t i = 0; i < sizeof blocked / sizeof blocked[0]; i++)
        assert(!ble_photo_mode_pending_allows(blocked[i]));
    puts("BLE photo mode: atomic key/prompt, consume-before-paint, legacy migration, blind recovery, failures, pending gate PASS");
    return 0;
}
