idf_component_register(
    SRCS "dht.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_event esp_timer
)