# 命定星宠应用源文件。基线 demo_*.c / ui_pixel*.c 保留在仓库中供参考，
# 但不编入应用固件（AGENTS.md 允许衍生应用替换 main/ 装配）。
file(GLOB SP_VOICE_ASSETS
     "${CMAKE_CURRENT_LIST_DIR}/assets/voice/*.adpcm")

idf_component_register(
    SRCS "main.c"
         "sp_app.c"
         "sp_scr_splash.c"
         "sp_model.c"
         "sp_text.c"
         "sp_record.c"
         "sp_adpcm.c"
         "sp_voice_manifest.c"
         "sp_sprites.c"
         "sp_store.c"
         "sp_clock.c"
         "sp_wifi.c"
         "sp_audio.c"
         "sp_state.c"
         "sp_ui.c"
         "sp_power.c"
         "sp_page_wizard.c"
         "sp_page_home.c"
         "sp_page_fortune.c"
         "sp_page_care.c"
         "sp_page_dex.c"
         "sp_page_settings.c"
         "assets/fonts/sp_font_16.c"
         "assets/fonts/sp_font_20.c"
    INCLUDE_DIRS "."
    REQUIRES bsp
             esp_event
             esp_hw_support
             esp_netif
             esp_timer
             esp_wifi
             nvs_flash
    EMBED_FILES ${SP_VOICE_ASSETS}
)
