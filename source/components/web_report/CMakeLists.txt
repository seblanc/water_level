# web_payload.c is pure C and always built (host-testable). The network part
# needs the Kconfig "Web report" options, which only exist when WT_WEB_ENABLE=y.
set(srcs "web_payload.c")
if(CONFIG_WT_WEB_ENABLE)
    list(APPEND srcs "web_report.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES level_report
    PRIV_REQUIRES wt_wifi esp_http_client mbedtls nvs_flash esp_timer esp_app_format
)
