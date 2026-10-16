# IDF v5.3 split the monolithic "driver" component into esp_driver_*; newer
# releases may drop the "driver" alias, so pick the requirement by version.
if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
    set(a02yyuw_uart_req esp_driver_uart)
    set(a02yyuw_gpio_req esp_driver_gpio)
else()
    set(a02yyuw_uart_req driver)
    set(a02yyuw_gpio_req "")
endif()

idf_component_register(
    SRCS "a02yyuw.c" "a02yyuw_parser.c" "a02yyuw_filter.c"
    INCLUDE_DIRS "include"
    REQUIRES ${a02yyuw_uart_req}
    PRIV_REQUIRES esp_timer ${a02yyuw_gpio_req}
)
