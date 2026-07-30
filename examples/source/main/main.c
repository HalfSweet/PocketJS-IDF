#include "pocketjs.h"
#include "pocketjs_app_hello.h"

#include "esp_log.h"

void app_main(void)
{
    const pocketjs_config_t config = POCKETJS_DEFAULT_CONFIG();
    pocketjs_t *runtime = NULL;
    ESP_ERROR_CHECK(pocketjs_create(&pocketjs_app_hello, &config, &runtime));

    pocketjs_frame_stats_t stats = { 0 };
    ESP_ERROR_CHECK(pocketjs_run_frame(runtime, NULL, &stats));
    ESP_LOGI(
        "pocketjs_example",
        "headless frame: js=%u us, core=%u us, total=%u us",
        (unsigned)stats.javascript_us,
        (unsigned)stats.core_tick_us,
        (unsigned)stats.total_us
    );
    pocketjs_destroy(runtime);
}
