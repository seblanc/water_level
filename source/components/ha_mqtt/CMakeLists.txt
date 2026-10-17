# ha_payload.c is pure C and always built (host-testable, keeps the include path
# valid for main). The network part needs the Kconfig "Home Assistant" options,
# which only exist when WT_HA_ENABLE=y (SYSTEM_ARCHITECTURE.md §5.2, §5.10.2).
set(srcs "ha_payload.c")
if(CONFIG_WT_HA_ENABLE)
    list(APPEND srcs "ha_config.c" "ha_mqtt.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES level_report
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash mqtt esp_timer esp_app_format
)
