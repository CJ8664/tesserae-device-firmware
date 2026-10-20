/* sdcard_lenient.c -- tolerate two optional SD init steps some cards refuse.
 *
 * ESP-IDF's sdmmc_card_init() aborts the whole mount when either of these
 * steps fails, and has no flag to skip them (sdmmc_init.c, IDF 5.5.2: the
 * SPI CRC step at :90, the SSR read at :160). Firmware #34 had two reTerminal
 * E1002 cards that work in other readers fail exactly there, both during the
 * 400 kHz probe phase, before SD_SPI_MAX_KHZ applies (so clock margin does
 * not help them):
 *
 *   - an 8 GB card rejects CMD59 (CRC on/off) with ESP_ERR_NOT_SUPPORTED.
 *     Data CRC is optional in SPI mode, and the sdspi host only turns its
 *     data-CRC check on when CMD59 succeeds (sdspi_host.c, the SD_CRC_ON_OFF
 *     branch after a successful command), so carrying on leaves CRC off
 *     consistently on both sides.
 *   - a 512 MB card times out reading the SD status register (ACMD13,
 *     ESP_ERR_TIMEOUT). The SSR only carries the allocation unit, erase
 *     timing, discard support and the current bus width. sdmmc_card_init()
 *     zeroes the card struct first and the SSR is decoded only on success,
 *     so a skipped read leaves card->ssr all zero: erase falls back to its
 *     per-sector timeout estimate, discard is SD-mode only, and FATFS
 *     mount / read / write never consult it.
 *
 * The linker wraps (-Wl,--wrap=..., added in src/CMakeLists.txt) route
 * sdmmc_init.c's calls through these functions. Only the one failure code
 * per step is forgiven; every other result, including success, is passed
 * through unchanged, so cards that mount today take the same path. Both
 * steps are defined in sdmmc_sd.c and called from sdmmc_init.c, which is
 * what lets --wrap intercept them (it only rewrites cross-object calls).
 */

#include <stdbool.h>

#include "esp_err.h"
#include "esp_log.h"
#include "sd_protocol_types.h"

static const char *TAG = "sdcard";

esp_err_t __real_sdmmc_init_spi_crc(sdmmc_card_t *card);
esp_err_t __real_sdmmc_init_sd_ssr(sdmmc_card_t *card);
esp_err_t __wrap_sdmmc_init_spi_crc(sdmmc_card_t *card);
esp_err_t __wrap_sdmmc_init_sd_ssr(sdmmc_card_t *card);

esp_err_t __wrap_sdmmc_init_spi_crc(sdmmc_card_t *card)
{
    static bool warned;
    esp_err_t err = __real_sdmmc_init_spi_crc(card);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        if (!warned) {
            warned = true;
            ESP_LOGW(TAG, "card refused CMD59 (CRC on); continuing without data CRC");
        }
        return ESP_OK;
    }
    return err;
}

esp_err_t __wrap_sdmmc_init_sd_ssr(sdmmc_card_t *card)
{
    static bool warned;
    esp_err_t err = __real_sdmmc_init_sd_ssr(card);
    if (err == ESP_ERR_TIMEOUT) {
        if (!warned) {
            warned = true;
            ESP_LOGW(TAG, "SD status (SSR) read timed out; continuing without it");
        }
        return ESP_OK;
    }
    return err;
}
