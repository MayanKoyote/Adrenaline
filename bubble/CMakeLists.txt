cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(adrenaline)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_APP_NAME "Adrenaline")
set(VITA_TITLEID  "PSPEMUCFW")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")
set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -a 0x2800000000000001")

add_executable(adrenaline
  main.c
  utils.c
  scr_printf.c
  font.c
)

target_link_libraries(adrenaline
  taihen_stub
  SceAppMgr_stub
  SceCtrl_stub
  SceDisplay_stub
  SceHttp_stub
  SceNet_stub
  SceNetCtl_stub
  SceSysmodule_stub
  ScePgf_stub_weak
)

vita_create_self(eboot.bin adrenaline UNSAFE)
vita_create_vpk(Adrenaline.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE pkg/sce_sys sce_sys
       pkg/eboot.pbp eboot.pbp
       pkg/frame.xml frame.xml
       ${CMAKE_BINARY_DIR}/kernel/adrenaline_kernel.skprx sce_module/adrenaline_kernel.skprx
       ${CMAKE_BINARY_DIR}/user/adrenaline_user.suprx sce_module/adrenaline_user.suprx
       ${CMAKE_BINARY_DIR}/vsh/adrenaline_vsh.suprx sce_module/adrenaline_vsh.suprx
       ${CMAKE_BINARY_DIR}/usbdevice/usbdevice.skprx sce_module/usbdevice.skprx
)
