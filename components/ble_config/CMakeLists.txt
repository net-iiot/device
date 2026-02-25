idf_component_register(
    SRCS "ble_config.cpp"
    INCLUDE_DIRS "."
    REQUIRES storage
    PRIV_REQUIRES bt freertos
)
