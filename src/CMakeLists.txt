# Each directory is one static library with one job. The dependency direction is
#   app -> config, modem, audio, ptt, display
# and only "modem" knows about the FreeDV backend.

# --- modem: the only code that touches freedv-backend / RADE -----------------
add_library(fdve_modem STATIC modem/Modem.cpp)
target_include_directories(fdve_modem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fdve_modem PRIVATE fdv_audio_pipeline fdv_util fdv_os_wrapper samplerate)
# The backend's logging header changes layout with these; keep them identical.
target_compile_definitions(fdve_modem PRIVATE ULOG_HAVE_TIME ULOG_NO_COLOR ULOG_ASYNC)

# --- audio --------------------------------------------------------------------
add_library(fdve_audio STATIC
    audio/AudioFactory.cpp
    audio/AudioPump.cpp
    audio/NullStream.cpp)
target_include_directories(fdve_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(FDVE_WITH_ALSA)
    target_sources(fdve_audio PRIVATE audio/AlsaStream.cpp)
    target_compile_definitions(fdve_audio PRIVATE FDVE_WITH_ALSA)
    target_link_libraries(fdve_audio PRIVATE ALSA::ALSA)
endif()

# --- ptt ----------------------------------------------------------------------
add_library(fdve_ptt STATIC
    ptt/GpioLine.cpp
    ptt/PttFactory.cpp)
target_include_directories(fdve_ptt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- display ------------------------------------------------------------------
add_library(fdve_display STATIC
    display/ConsoleDisplay.cpp
    display/DisplayFactory.cpp)
target_include_directories(fdve_display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- config: the TOML file -> Config struct -----------------------------------
add_library(fdve_config STATIC app/Config.cpp)
target_include_directories(fdve_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fdve_config PRIVATE tomlplusplus::tomlplusplus)

# --- app: PTT state machine, wiring ---------------------------------------------
add_library(fdve_app STATIC app/Controller.cpp)
target_link_libraries(fdve_app PUBLIC fdve_config fdve_modem fdve_audio fdve_ptt fdve_display)

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
