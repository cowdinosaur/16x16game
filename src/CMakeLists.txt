idf_component_register(SRCS "main.cpp" "WS2812.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver freertos)