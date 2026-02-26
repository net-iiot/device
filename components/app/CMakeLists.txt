idf_component_register(
    SRCS "app.cpp"
    INCLUDE_DIRS "."
    REQUIRES sys storage ble_config ble_alert
    PRIV_REQUIRES driver
)
