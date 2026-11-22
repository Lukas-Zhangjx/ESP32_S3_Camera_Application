# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS main.c
    INCLUDE_DIRS .
    PRIV_REQUIRES camera wifi mqtt_bridge stream_service storage servo
)
