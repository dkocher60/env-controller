idf_component_register(
    SRCS "mqtt_app.c" "mqtt_util.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt
    PRIV_REQUIRES esp_event tcp_transport
)

if(CONFIG_MQTT_USE_SSL)
    target_add_binary_data(${COMPONENT_LIB} "certs/mqtt_broker.pem" TEXT)
endif()