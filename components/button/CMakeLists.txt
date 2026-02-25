idf_component_register(
    SRCS "button.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver freertos esp_timer
)
