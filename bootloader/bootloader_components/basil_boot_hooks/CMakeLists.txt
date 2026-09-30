idf_component_register(SRCS "hooks.c"
                       PRIV_REQUIRES bootloader_support esp_rom log spi_flash)

# Hooks are weak overrides nothing else references; force them into the link.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u bootloader_hooks_include")
