#include "pocketjs.h"

#include "esp_log.h"

void app_main(void)
{
    ESP_LOGI(
        "pocketjs_example",
        "PocketJS-IDF target=%s host ABI=%u",
        POCKETJS_TARGET_ID,
        (unsigned)POCKETJS_HOST_ABI
    );
}
