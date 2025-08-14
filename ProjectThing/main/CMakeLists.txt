file(GLOB_RECURSE CODE_SOURCES ./*.c)

idf_component_register(
    SRC_DIRS
        "."
        # "app"
        "gui"
        "gui/font"
        "gui/image"
        "app"
        # "rmaker"

    INCLUDE_DIRS
        "."
        "gui"
        "app"
        # "app"
        # "rmaker"
        
    SRCS
        ${CODE_SOURCES}

)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_CONF_PATH="\"lv_conf.h\"")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format" "-Wno-deprecated-declarations")
# target_compile_definitions(${COMPONENT_TARGET} PRIVATE "-D RMAKER_DEMO_PROJECT_NAME=\"${CMAKE_PROJECT_NAME}\"")

set_source_files_properties(
    ${CODE_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE
    )

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
