idf_component_register(
    SRCS
        "main.cpp"
        "button.cpp"
        "storage.cpp"
        "ble_config.cpp"
        "ble_alert.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        driver
        freertos
        nvs_flash
        bt
        esp_timer
)
