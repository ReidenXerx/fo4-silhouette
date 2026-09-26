cmake_minimum_required(VERSION 3.21)

# The version lives in VERSION (one line, x.y.z) so the generator, the plugin and the release
# archive can never disagree about it.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SILHOUETTE_VERSION)
string(STRIP "${SILHOUETTE_VERSION}" SILHOUETTE_VERSION)

# Static libraries, shared CRT: an F4SE plugin is one DLL, with no spdlog.dll or fmt.dll beside it.
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")

project(
	Silhouette
	VERSION ${SILHOUETTE_VERSION}
	LANGUAGES CXX
)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "")

# S-75: one build for OG 1.10.163, NG 1.10.984 and AE 1.11.x through Runtime Database (CommonLibF4RD):
# addresses resolved at run time from f4rd-runtime.bin, class layouts guarded in Game.cpp. OFF builds
# the classic OG-only plugin on alandtse's CommonLibF4 (S-18), which refuses any other runtime.
option(SILHOUETTE_RUNTIME_DATABASE "Build on CommonLibF4RD for OG, NG and AE" ON)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
# __FILE__ without this machine's folders (0.2.1 zip audit, 2026-09-26): the compiler writes every source's full
# path into the dll, which named the Windows user in every release since 0.1.0. Trimmed before CommonLib so its
# paths go too, and in the build rather than the packaging, so the dev dll and the release dll stay one build.
if(MSVC)
	file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/" SILHOUETTE_TRIM_PREFIX)
	add_compile_options("/d1trimfile:${SILHOUETTE_TRIM_PREFIX}")
endif()
if(SILHOUETTE_RUNTIME_DATABASE)
	add_subdirectory("extern/CommonLibF4RD/CommonLibF4" "CommonLibF4")
else()
	set(ENABLE_FALLOUT_F4 ON  CACHE BOOL "" FORCE)
	set(ENABLE_FALLOUT_NG OFF CACHE BOOL "" FORCE)
	set(ENABLE_FALLOUT_VR OFF CACHE BOOL "" FORCE)
	add_subdirectory("extern/CommonLibF4/CommonLibF4" "CommonLibF4")
endif()

set(SOURCES
	"src/PCH.h"
	"src/Catalog.h"
	"src/Catalog.cpp"
	"src/CoSave.h"
	"src/CoSave.cpp"
	"src/Director.h"
	"src/Director.cpp"
	"src/EventSources.h"
	"src/EventSources.cpp"
	"src/Game.h"
	"src/Game.cpp"
	"src/Papyrus.h"
	"src/Papyrus.cpp"
	"src/Plan.h"
	"src/Plan.cpp"
	"src/Presets.h"
	"src/Presets.cpp"
	"src/Registry.h"
	"src/Registry.cpp"
	"src/Rules.h"
	"src/Rules.cpp"
	"src/Sinks.h"
	"src/Sinks.cpp"
	"src/main.cpp"
)

add_library("${PROJECT_NAME}" SHARED ${SOURCES})

target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)

target_compile_definitions(
	"${PROJECT_NAME}"
	PRIVATE
		"SH_PROJECT_NAME=\"${PROJECT_NAME}\""
		"SH_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
		"SH_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
		"SH_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
		"SH_VERSION_STRING=\"${PROJECT_VERSION}\""
		"$<$<BOOL:${SILHOUETTE_RUNTIME_DATABASE}>:SH_RUNTIME_DATABASE=1>"
)

target_include_directories("${PROJECT_NAME}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries("${PROJECT_NAME}" PRIVATE CommonLibF4::CommonLibF4 nlohmann_json::nlohmann_json)

target_precompile_headers("${PROJECT_NAME}" PRIVATE "src/PCH.h")

if(MSVC)
	# A PDB for the Release DLL too: a crash log names our functions only with one. /DEBUG turns the
	# linker's /OPT:REF and /OPT:ICF off by default, so they are asked for again.
	target_compile_options("${PROJECT_NAME}" PRIVATE "/permissive-" "/Zc:preprocessor" "/utf-8" "/W4" "/wd4201" "$<$<CONFIG:Release>:/Zi>")
	target_link_options("${PROJECT_NAME}" PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF;/PDBALTPATH:%_PDB%>")
endif()

# Everything with no game in it -- the catalog, the rules, the plans, the co-save bytes and the whole
# director -- is tested without one.
add_executable(
	SilhouetteTests
	"tests/main.cpp"
	"src/Catalog.cpp"
	"src/Director.cpp"
	"src/Plan.cpp"
	"src/Presets.cpp"
	"src/Registry.cpp"
	"src/Rules.cpp"
)
target_compile_features(SilhouetteTests PRIVATE cxx_std_23)
target_compile_definitions(SilhouetteTests PRIVATE SH_OFFLINE_TESTS=1)
target_include_directories(SilhouetteTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_precompile_headers(SilhouetteTests PRIVATE "src/PCH.h")
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(SilhouetteTests PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
	target_compile_options(SilhouetteTests PRIVATE "/permissive-" "/Zc:preprocessor" "/utf-8" "/W4")
endif()
