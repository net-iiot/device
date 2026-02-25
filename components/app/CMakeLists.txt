idf_component_register(
    SRCS "app.cpp"
    INCLUDE_DIRS "."
    REQUIRES alert_runner sys storage ble_config
    PRIV_REQUIRES driver
)
