idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES a02yyuw level_report ha_mqtt web_report wt_wifi nvs_flash esp_app_format spi_flash
)
