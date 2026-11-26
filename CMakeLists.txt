cmake_minimum_required(VERSION 3.24)
project(foo_ytpyp LANGUAGES CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "foo_ytpyp only builds for 64-bit Windows with MSVC.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# foobar2000 SDK. The plugin uses plain Win32 for its UI, so libPPUI, the SDK
# helpers and WTL/ATL aren't needed.
set(FOO_PPUI OFF)
set(FOO_SDK_HELPERS OFF)
add_subdirectory(foosdk)
set(FOOSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/foosdk")

# ytpipe_core (Rust). Cargo is always run and decides for itself whether
# anything needs rebuilding.
find_program(CARGO cargo REQUIRED)
set(YTPIPE_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ytpipe_core")
set(YTPIPE_CORE_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo")
set(YTPIPE_CORE_LIB "${YTPIPE_CORE_TARGET_DIR}/x86_64-pc-windows-msvc/release/ytpipe_core.lib")
add_custom_target(ytpipe_core_build
	COMMAND "${CARGO}" build --release --lib
		--manifest-path "${YTPIPE_CORE_DIR}/Cargo.toml"
		--target x86_64-pc-windows-msvc
		--target-dir "${YTPIPE_CORE_TARGET_DIR}"
	BYPRODUCTS "${YTPIPE_CORE_LIB}"
	COMMENT "Building ytpipe_core with cargo"
	USES_TERMINAL
	VERBATIM
)
add_library(ytpipe_core STATIC IMPORTED)
set_target_properties(ytpipe_core PROPERTIES IMPORTED_LOCATION "${YTPIPE_CORE_LIB}")
add_dependencies(ytpipe_core ytpipe_core_build)

# The component itself. The DLL must stay named foo_ytpipe.dll, because
# main.cpp checks for that name with VALIDATE_COMPONENT_FILENAME.
add_library(foo_ytpipe MODULE
	src/main.cpp
	src/input_yt.cpp
	src/menu.cpp
	src/ui_dialog.cpp
	src/ui_dialog.h
	src/ytpipe_api.h
)
target_include_directories(foo_ytpipe PRIVATE
	"${FOOSDK_DIR}/sdk"
	"${FOOSDK_DIR}/sdk/foobar2000"
	src
)
target_compile_definitions(foo_ytpipe PRIVATE _UNICODE UNICODE STRICT)
target_compile_options(foo_ytpipe PRIVATE /MP /W3 /sdl)
target_link_libraries(foo_ytpipe PRIVATE
	foosdk
	"${FOOSDK_DIR}/sdk/foobar2000/shared/shared-x64.lib"
	ytpipe_core
	# System libraries needed by the Rust standard library and ureq.
	ws2_32 crypt32 userenv bcrypt ntdll synchronization
	comctl32
)
set_property(TARGET foo_ytpipe PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
# ytpipe_core is always a release build, which asks for the release CRT. Debug
# builds of the component use the debug CRT, so drop the release one there.
target_link_options(foo_ytpipe PRIVATE $<$<CONFIG:Debug>:/NODEFAULTLIB:msvcrt.lib>)

# Package foo_ytpipe.dll as foo_ytpyp.fb2k-component (a zip) next to the DLL.
add_custom_command(TARGET foo_ytpipe POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E tar cf foo_ytpyp.fb2k-component --format=zip foo_ytpipe.dll
	WORKING_DIRECTORY "$<TARGET_FILE_DIR:foo_ytpipe>"
	COMMENT "Packaging foo_ytpyp.fb2k-component"
	VERBATIM
)
