idf_component_register(SRCS "main.c" "bmp280.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_wifi esp_netif esp_event esp_http_server nvs_flash esp_timer esp_system esp_hw_support)

# Course: warnings on our component, not on all SDK dependencies.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow -Wformat=2)
