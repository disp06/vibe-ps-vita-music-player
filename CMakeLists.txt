cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(vibe C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Vibe")
set(VITA_TITLEID  "VIBE00001")
set(VITA_VERSION  "01.01")
# ATTRIBUTE: 0x8000 no communication zone, 0x8 keep running when deactivated
# (background audio), 0x80000 no system suspend (power button blanks instead)
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1 -d ATTRIBUTE=0x88008")

include_directories(${VITASDK}/arm-vita-eabi/include/freetype2)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -ffast-math -Wall -Wno-unused-parameter -std=gnu11")

add_executable(vibe
  src/main_vita.c src/app.c src/ui.c src/font.c src/shape.c src/i18n.c src/gfx_common.c src/gfx_vita.c
  src/skin_metro.c src/skin_retro.c src/skin_minimal.c
  src/player.c src/decoder.c src/dec_mp3.c src/dec_ogg.c src/dec_flac.c src/dec_wav.c
  src/sys_vita.c src/config.c src/browser.c)

target_link_libraries(vibe
  vita2d freetype png jpeg z bz2 mpg123 vorbisfile vorbis ogg FLAC m
  SceDisplay_stub SceGxm_stub SceCtrl_stub SceTouch_stub SceAudio_stub ScePower_stub
  SceSysmodule_stub SceCommonDialog_stub SceAppUtil_stub SceAppMgr_stub SceRtc_stub
  SceShellSvc_stub SceKernelDmacMgr_stub SceIofilemgr_stub)

vita_create_self(eboot.bin vibe UNSAFE STRIPPED REL_OPTIMIZE)
vita_create_vpk(vibe.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE assets/fonts/OpenSans-Light.ttf assets/fonts/OpenSans-Light.ttf
  FILE assets/fonts/OpenSans-Regular.ttf assets/fonts/OpenSans-Regular.ttf
  FILE assets/fonts/ShareTechMono.ttf assets/fonts/ShareTechMono.ttf
  FILE assets/fonts/NotoSansSC.ttf assets/fonts/NotoSansSC.ttf
  FILE assets/fonts/NotoSansDevanagari.ttf assets/fonts/NotoSansDevanagari.ttf
  FILE assets/fonts/NotoSansArabic.ttf assets/fonts/NotoSansArabic.ttf)
