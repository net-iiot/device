idf_component_register(
    SRCS "alert_runner.cpp"
    INCLUDE_DIRS "."
    REQUIRES button sys storage ble_alert
    PRIV_REQUIRES driver freertos esp_system
)
