list(APPEND requires "protobuf-c" "protocomm" "nvs_flash" "esp_wifi" "driver" "app_update")

idf_component_register(SRC_DIRS "./src"
                       INCLUDE_DIRS "./src"
                       REQUIRES "${requires}")

idf_component_get_property(COMPONENT_DIR protocomm COMPONENT_DIR)
target_include_directories(${COMPONENT_TARGET} PUBLIC
"${COMPONENT_DIR}/proto-c"
)