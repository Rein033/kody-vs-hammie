# The fight itself (game.c) is pure C; the AO brings the SDK: the panel, the buttons, the tone and
# the vibration motor.
idf_component_register(
    SRCS "app_main.c" "fight_ao.c" "game.c" "font.c" "sound.c"
    INCLUDE_DIRS "."
    REQUIRES kode
    PRIV_REQUIRES kdrv khal esp_hw_support esp_timer esp_system
)
