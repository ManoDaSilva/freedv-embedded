# Each directory is one static library with one job. The dependency direction is
#   app -> ui -> config, audio, ptt, display   and   app -> modem
# and only "modem" knows about the FreeDV backend.

# --- log: level-filtered messages and system readings, used by everything --------------------------
add_library(fdve_log STATIC util/Log.cpp util/SystemInfo.cpp)
target_include_directories(fdve_log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- modem: the only code that touches freedv-backend / RADE -----------------
add_library(fdve_modem STATIC modem/Modem.cpp)
target_include_directories(fdve_modem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fdve_modem PUBLIC fdve_log PRIVATE fdv_audio_pipeline fdv_util fdv_os_wrapper samplerate)
# The backend's logging header changes layout with these; keep them identical.
target_compile_definitions(fdve_modem PRIVATE ULOG_HAVE_TIME ULOG_NO_COLOR ULOG_ASYNC)

# --- audio --------------------------------------------------------------------
add_library(fdve_audio STATIC
    audio/AnalogPath.cpp
    audio/AudioFactory.cpp
    audio/AudioPump.cpp
    audio/Mixer.cpp
    audio/NullStream.cpp)
target_include_directories(fdve_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fdve_audio PUBLIC fdve_log PRIVATE samplerate)
if(FDVE_WITH_ALSA)
    target_sources(fdve_audio PRIVATE audio/AlsaStream.cpp)
    target_compile_definitions(fdve_audio PRIVATE FDVE_WITH_ALSA)
    target_link_libraries(fdve_audio PRIVATE ALSA::ALSA)
endif()

# --- ptt ----------------------------------------------------------------------
add_library(fdve_ptt STATIC
    ptt/ConsoleKeys.cpp
    ptt/GpioLine.cpp
    ptt/PttFactory.cpp)
target_include_directories(fdve_ptt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- display ------------------------------------------------------------------
add_library(fdve_display STATIC
    display/ConsoleDisplay.cpp
    display/DisplayFactory.cpp
    display/Framebuffer.cpp
    display/Ssd1306Display.cpp
    display/StatusScreen.cpp)
target_include_directories(fdve_display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fdve_display PUBLIC fdve_log)

# --- ui: knob input, menu engine, settings menu, config file writer ----------------
add_library(fdve_ui STATIC
    ui/Menu.cpp
    ui/MenuInput.cpp
    ui/SettingsMenu.cpp
    ui/TomlPatcher.cpp)
target_include_directories(fdve_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fdve_ui PUBLIC fdve_config fdve_display fdve_audio fdve_ptt fdve_log)

# --- config: the TOML file -> Config struct -----------------------------------
add_library(fdve_config STATIC app/Config.cpp)
target_include_directories(fdve_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fdve_config PUBLIC fdve_log PRIVATE tomlplusplus::tomlplusplus)

# --- app: PTT state machine, wiring ---------------------------------------------
add_library(fdve_app STATIC app/Controller.cpp)
target_link_libraries(fdve_app PUBLIC fdve_config fdve_modem fdve_audio fdve_ptt fdve_display fdve_ui)

add_executable(freedv-embedded main.cpp)
target_link_libraries(freedv-embedded PRIVATE fdve_app Threads::Threads)
target_compile_definitions(freedv-embedded PRIVATE FDVE_VERSION="${PROJECT_VERSION}")
set_target_properties(freedv-embedded PROPERTIES
    BUILD_RPATH "${FDVE_RADE_LIB_DIR}"
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/freedv-embedded")

install(TARGETS freedv-embedded RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${FDVE_RADE_LIB_DIR}/
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/freedv-embedded
    FILES_MATCHING PATTERN "librade.so*")
