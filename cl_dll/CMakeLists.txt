cmake_minimum_required(VERSION 3.10)
project(client)

set(CLIENT_LIB client)

file(GLOB CS_CLIENT_SRC "*.cpp")
file(GLOB CS_EV_SRC "events/*.cpp")
file(GLOB CS_WPN_SRC "cs_wpn/*.cpp")
file(GLOB CS_WPNSH_SRC "../dlls/wpn_shared/*.cpp")
file(GLOB CS_HUD_SRC "hud/*.cpp")
file(GLOB CS_STUDIO_SRC "studio/*.cpp")
file(GLOB CS_INPUT_SRC "input/*.cpp")
file(GLOB CS_CHROME_SRC "chrome/*.cpp")
file(GLOB CS_PM_SRC "../pm_shared/*.cpp")
file(GLOB CS_GAMSH_SRC "../game_shared/*.cpp")
file(GLOB MINIUTL_SRC "../3rdparty/mainui_cpp/miniutl/*.cpp")

list(REMOVE_ITEM CS_CLIENT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/ev_hldm.cpp")
list(REMOVE_ITEM CS_INPUT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/input/input_sdl.cpp")
list(REMOVE_ITEM CS_CLIENT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/inputw32.cpp")
list(REMOVE_ITEM CS_WPN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/cs_wpn/cs_baseentity.cpp")
list(APPEND CS_CLIENT_SRC ../common/interface.cpp)
list(APPEND CS_CLIENT_SRC ../public/utflib.cpp)

list(APPEND CS_CLIENT_SRC ${CS_EV_SRC})
list(APPEND CS_CLIENT_SRC ${CS_WPN_SRC})
list(APPEND CS_CLIENT_SRC ${CS_WPNSH_SRC})
list(APPEND CS_CLIENT_SRC ${CS_HUD_SRC})
list(APPEND CS_CLIENT_SRC ${CS_STUDIO_SRC})
list(APPEND CS_CLIENT_SRC ${CS_PM_SRC})
list(APPEND CS_CLIENT_SRC ${CS_INPUT_SRC})
list(APPEND CS_CLIENT_SRC ${CS_CHROME_SRC})
list(APPEND CS_CLIENT_SRC ${CS_GAMSH_SRC})
list(APPEND CS_CLIENT_SRC ${MINIUTL_SRC})

include_directories(
	include
	include/hud
	include/studio
	include/math
	../cl_dll
	../common
	../engine
	../pm_shared
	../dlls
	../game_shared
	../public/
	../public/cl_dll
	../3rdparty/mainui_cpp
	../3rdparty/mainui_cpp/controls
	../3rdparty/mainui_cpp/menus
	../3rdparty/mainui_cpp/miniutl)

add_definitions(-DCLIENT_WEAPONS -DCLIENT_DLL -DSTDINT_H=<cstdint>)

if(NOT MSVC)
	add_definitions(-DLINUX -D_LINUX)
	add_definitions(-Dstricmp=strcasecmp -D_strnicmp=strncasecmp -Dstrnicmp=strncasecmp -fms-extensions)
else()
	add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE)
endif()

add_library(${CLIENT_LIB} SHARED ${CS_CLIENT_SRC})
# =========================================================
# CEF CONFIGURATION 
# =========================================================

option(CS16CLIENT_ENABLE_CEF "Enable CEF integration" ON)
set(CEF_ROOT "" CACHE PATH "Path to CEF distribution root")

set(CS16CLIENT_HAS_CEF OFF CACHE BOOL "CEF enabled")
set(CS16CLIENT_CEF_ROOT_RESOLVED "" CACHE PATH "Resolved CEF root")

if(CS16CLIENT_ENABLE_CEF AND NOT "${CEF_ROOT}" STREQUAL "")

    get_filename_component(_cef_root "${CEF_ROOT}" ABSOLUTE)

    # Validation
    if(NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
		message(FATAL_ERROR "Invalid CEF root (missing include/cef_app.h)")
	endif()

	if(NOT EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
		message(FATAL_ERROR "Invalid CEF root (missing cmake module)")
	endif()

    set(CS16CLIENT_HAS_CEF ON CACHE BOOL "" FORCE)
    set(CS16CLIENT_CEF_ROOT_RESOLVED "${_cef_root}" CACHE PATH "" FORCE)

    target_compile_definitions(${CLIENT_LIB} PRIVATE
        CS16CLIENT_ENABLE_CEF=1
    )

    target_include_directories(${CLIENT_LIB} PRIVATE
        "${_cef_root}"
        "${_cef_root}/include"
    )

    include(${CMAKE_SOURCE_DIR}/cmake/CEFIntegration.cmake)
	set(CS16CLIENT_CEF_ROOT "${CEF_ROOT}")
    cs16client_setup_cef(${CLIENT_LIB})

endif()

set_target_postfix(${CLIENT_LIB})

install(TARGETS ${CLIENT_LIB}
	DESTINATION "${GAME_DIR}/${CLIENT_INSTALL_DIR}/"
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
	    GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE)

# Install PDB file on Windows
if(MSVC)
	install(FILES $<TARGET_PDB_FILE:${CLIENT_LIB}>
		DESTINATION "${GAME_DIR}/${CLIENT_INSTALL_DIR}/" OPTIONAL)
endif()