idf_component_register(
    SRCS "stream_service.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES camera mqtt_bridge esp_timer
)
