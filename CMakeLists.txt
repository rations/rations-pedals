# Rations Pedals — five stompbox VST3 plug-ins for Linux and Windows.
# Copyright (c) 2026 rations. MIT licence (see LICENSE).
#
# ONE PROJECT, FIVE BUNDLES. Each pedal is a separate .vst3 with its own plug-in UIDs and its own
# factory; nothing is shared between them at run time and none of them requires another to be
# installed. What they share is source, and sharing it here rather than in five repositories is
# what keeps their state format, MIDI learn and bus negotiation from drifting apart.
#
# Configure:
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
# Windows, cross:
#   cmake -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake \
#         -DCMAKE_BUILD_TYPE=Release && cmake --build build-win --parallel

cmake_minimum_required(VERSION 3.25.0)

project(rations-pedals VERSION 0.1.0)

set(RPEDALS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(RPEDALS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(RPEDALS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(RPEDALS_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(RPEDALS_VERSION_BUILD 1)

if(WIN32)
    set(RPEDALS_WINDOWS TRUE)
endif()

# ---------------------------------------------------------------------------
# What to build. Both default ON, so building this repository on its own is unchanged.
#
# THEY EXIST BECAUSE THIS PROJECT IS ALSO CONSUMED AS A SUBDIRECTORY. NAMp Rack pins this tree as a
# submodule and add_subdirectory()s it, so that the five bundles it ships are built by its own
# build and pinned to one commit of this one, rather than copied in from somewhere. That was
# already anticipated here -- the SDK block below is guarded `if(NOT TARGET sdk)` precisely so a
# parent that has already added the SDK is not made to add it twice -- but two things were not:
#
#   * `panelrender` is a target NAME, and the rack has one of its own. Two add_executable() calls
#     for one name is a hard CMake error, and it was the only collision between the two trees.
#   * The rack ships bundles and nothing else. It is itself the host, so five JACK standalones and
#     three offline proofs are build time spent on binaries it will never package.
#
# The proofs are not weakened by this. pedalcheck, loadall and panelrender are THIS project's
# gates and they run in THIS project's build and release scripts, which is where the pedals are
# proved before they are pinned. A consumer switching them off is not skipping a check; it is
# declining to re-run somebody else's.
# ---------------------------------------------------------------------------
option(RPEDALS_BUILD_TOOLS       "Build the offline proof and render tools" ON)
option(RPEDALS_BUILD_STANDALONES "Build the five JACK standalones (Linux)"  ON)

# ---------------------------------------------------------------------------
# Symbol visibility: hidden by default, for everything we compile.
#
# A correctness requirement, not an optimisation, and this project is the exact situation that
# makes it one. GCC gives a function-local static inside an inline function STB_GNU_UNIQUE
# binding, and glibc resolves those through a table shared by the whole link-map namespace —
# dlopen's RTLD_LOCAL does not scope it. Five plug-ins built from the SAME source, loaded into one
# host at once, would therefore be sharing one another's statics. Hidden visibility plus
# --exclude-libs,ALL on each bundle is what keeps five copies five copies.
#
# scripts/makedist-linux.sh asserts that no such symbol survives into any shipped .so, because
# this is the kind of thing that is correct until somebody adds a target without the flags.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(RPEDALS_WINDOWS)
    # MinGW derives neither UNICODE macro from the other, and the SDK's dllmain.cpp writes ANSI
    # into a char16_t buffer without them. NOMINMAX and WIN32_LEAN_AND_MEAN keep windows.h from
    # defining min/max as macros over the std:: ones this code uses everywhere.
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# The VST 3 SDK.
#
# The in-tree submodule, pinned to one commit, is what builds. A pristine checkout of that same
# commit is kept OUTSIDE this tree as the read-only reference every SDK fact is verified against,
# so that reading the SDK and building against it cannot drift into the same copy. -DVST3_SDK_DIR
# points the build at another copy and must produce an identical build.
# ---------------------------------------------------------------------------
if(NOT TARGET sdk)
    set(VST3_SDK_DIR "${RPEDALS_ROOT}/vst3sdk" CACHE PATH "VST 3 SDK source tree")
    if(NOT EXISTS "${VST3_SDK_DIR}/pluginterfaces/vst/vsttypes.h")
        message(FATAL_ERROR
            "[RationsPedals] no VST 3 SDK at '${VST3_SDK_DIR}'. Run\n"
            "    git submodule update --init --recursive vst3sdk\n"
            "or configure with -DVST3_SDK_DIR=/path/to/vst3sdk.")
    endif()

    # The editor is drawn directly onto a Cairo surface; VSTGUI is neither used nor built.
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    # ON for the validator and moduleinfotool, which is how every bundle here is checked.
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
    set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
    if(CMAKE_CROSSCOMPILING)
        # Its POST_BUILD step runs a freshly built moduleinfotool.exe, which cannot execute on
        # the build host. scripts/makedist-windows.sh generates moduleinfo.json under Wine instead.
        set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
    endif()

    add_subdirectory(${VST3_SDK_DIR} vst3sdk)

    # The SDK's helper functions read these DIRECTORY-scoped variables, and the SDK sets them only
    # in its own scope. Without public_sdk_SOURCE_DIR, smtg_target_add_library_main silently adds
    # no entry point and the bundle loads as an empty module.
    set(public_sdk_SOURCE_DIR "${VST3_SDK_DIR}/public.sdk")
    set(pluginterfaces_SOURCE_DIR "${VST3_SDK_DIR}/pluginterfaces")

    if(WIN32)
        # dataexchange.cpp does not compile with MinGW: alignedalloc.h picks _aligned_malloc on
        # _MSC_VER and otherwise C11 aligned_alloc, and MinGW's msvcrt has neither. The test is
        # really "is this the Microsoft CRT". Dropping the file is safe — nothing here uses
        # IDataExchangeHandler — and it is dropped rather than patched because the SDK is a pinned
        # submodule and a local edit would vanish on the next submodule update.
        get_target_property(_sdk_sources sdk SOURCES)
        list(FILTER _sdk_sources EXCLUDE REGEX "utility/dataexchange\\.cpp$")
        set_target_properties(sdk PROPERTIES SOURCES "${_sdk_sources}")
        message(STATUS "[RationsPedals] dropped dataexchange.cpp from the SDK (unused; MinGW has no std::aligned_alloc)")

        # The SDK's command-line tools define wmain. MinGW only wires wmain to the CRT entry point
        # when -municode is on the LINK line; without it the build dies on an undefined WinMain.
        foreach(_tool validator moduleinfotool)
            if(TARGET ${_tool})
                target_link_options(${_tool} PRIVATE -municode)
            endif()
        endforeach()
    endif()
endif()

# ---------------------------------------------------------------------------
# Neutralise the SDK's Windows folder-icon step when cross-compiling.
#
# smtg_target_make_plugin_package() adds a POST_BUILD step that runs "attrib +s", a cmd.exe
# built-in that cannot exist on a Linux build host, and the SDK offers no switch for it. Redefining
# the function here — before the first smtg_add_vst3plugin() call, so this is the definition those
# calls resolve to — replaces it without touching the pinned submodule. What is lost is an Explorer
# folder icon, which is cosmetic, is Steinberg's logo rather than ours, and does not survive a ZIP
# in any case.
# ---------------------------------------------------------------------------
if(CMAKE_CROSSCOMPILING AND WIN32)
    function(smtg_target_add_folder_icon target icon)
        # Deliberately empty. See above.
    endfunction()
endif()

# ---------------------------------------------------------------------------
# Graphics: Cairo, FreeType, and on Linux the X11 backend as well.
#
# No fontconfig on Windows and no cairo-xlib: the Win32 view composes into an image surface backed
# by a DIB section and blits it with one BitBlt, and both fonts are loaded by path out of the
# bundle. CAIRO_WIN32_STATIC_BUILD is required for a static Cairo or every cairo_* call fails to
# link as __imp_cairo_*.
# ---------------------------------------------------------------------------
# The IPlugView base, one platform or the other. src/platform/plugview.h is the seam that picks
# between them; nothing else in src/ includes a platform header.
if(RPEDALS_WINDOWS)
    set(RPEDALS_PLATFORM_VIEW_SOURCES ${RPEDALS_ROOT}/src/platform/win32plugview.cpp)
else()
    set(RPEDALS_PLATFORM_VIEW_SOURCES ${RPEDALS_ROOT}/src/platform/x11plugview.cpp)
endif()

find_package(PkgConfig REQUIRED)
if(RPEDALS_WINDOWS)
    set(PKG_CONFIG_ARGN --static)
    pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo cairo-ft freetype2)
    target_compile_definitions(PkgConfig::CAIRO INTERFACE CAIRO_WIN32_STATIC_BUILD)
    set(RPEDALS_PLATFORM_GUI_LIBS user32 gdi32 msimg32)
else()
    pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo cairo-ft freetype2 fontconfig)
    pkg_check_modules(CAIROX11 REQUIRED IMPORTED_TARGET cairo-xlib x11)
    set(RPEDALS_PLATFORM_GUI_LIBS PkgConfig::CAIROX11)
endif()

# ---------------------------------------------------------------------------
# PedalsGfx — the drawing layer, shared by the five plug-ins and by the offline render tools.
#
# Deliberately free of both X11 and VST3, which is what lets tools/panelrender link it and run on
# Windows before any plug-in code exists.
# ---------------------------------------------------------------------------
add_library(PedalsGfx STATIC
    ${RPEDALS_ROOT}/src/gfx/canvas.cpp
    ${RPEDALS_ROOT}/src/gfx/fontstack.cpp
    ${RPEDALS_ROOT}/src/gfx/image.cpp
    ${RPEDALS_ROOT}/src/gfx/svg.cpp
    ${RPEDALS_ROOT}/src/gfx/resourcestore.cpp
    ${RPEDALS_ROOT}/src/platform/respath.cpp
)
target_include_directories(PedalsGfx PUBLIC ${RPEDALS_ROOT}/src)
target_link_libraries(PedalsGfx PUBLIC PkgConfig::CAIRO ${CMAKE_DL_LIBS})
target_compile_features(PedalsGfx PUBLIC cxx_std_17)
target_compile_options(PedalsGfx PRIVATE -Wall)
set_target_properties(PedalsGfx PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Resources. Each bundle carries ONLY what its own face draws: its enclosure, plus the chrome
# footswitch cap, the dial, the two lamps, the two switch rings and the clear icon, which are the
# same art on all five faces. The *-base.png files are the untrimmed exports gui/ reads and are
# deliberately absent.
# ---------------------------------------------------------------------------
set(RPEDALS_FONT_FILES
    fonts/Michroma-Regular.ttf
    fonts/Roboto-Regular.ttf
)
set(RPEDALS_SHARED_IMG_FILES
    img/dial.png
    img/led_on.png
    img/led_off.png
    img/switch_up_ring.png
    img/switch_down_ring.png
    img/pedal_switch.png
)

set(RPEDALS_FONT_PATHS "")
foreach(_res IN LISTS RPEDALS_FONT_FILES)
    list(APPEND RPEDALS_FONT_PATHS "${RPEDALS_ROOT}/resources/${_res}")
endforeach()
set(RPEDALS_SHARED_IMG_PATHS "")
foreach(_res IN LISTS RPEDALS_SHARED_IMG_FILES)
    list(APPEND RPEDALS_SHARED_IMG_PATHS "${RPEDALS_ROOT}/resources/${_res}")
endforeach()

# ---------------------------------------------------------------------------
# The five plug-ins.
#
# smtg_add_vst3plugin's PACKAGE_NAME / SOURCES_LIST form (vst3sdk/cmake/modules/
# SMTG_AddVST3Library.cmake) is what allows more than one plug-in per project; the bundle folder
# takes the package name and the inner binary takes the target name.
# ---------------------------------------------------------------------------
set(RPEDALS_PEDALS boost chorus flanger delay reverb)
set(RPEDALS_TARGET_boost   RationsBoost)
set(RPEDALS_TARGET_chorus  RationsChorus)
set(RPEDALS_TARGET_flanger RationsFlanger)
set(RPEDALS_TARGET_delay   RationsDelay)
set(RPEDALS_TARGET_reverb  RationsReverb)
set(RPEDALS_DISPLAY_boost   "Rations Boost")
set(RPEDALS_DISPLAY_chorus  "Rations Chorus")
set(RPEDALS_DISPLAY_flanger "Rations Flanger")
set(RPEDALS_DISPLAY_delay   "Rations Delay")
set(RPEDALS_DISPLAY_reverb  "Rations Reverb")

foreach(_pedal IN LISTS RPEDALS_PEDALS)
    set(_target ${RPEDALS_TARGET_${_pedal}})

    # version.h is per plug-in — it carries stringPluginName and stringOriginalFilename, which the
    # bundle's own version resource and the factory's class names are built from.
    set(RPEDALS_PLUGIN_NAME ${_target})
    configure_file(${RPEDALS_ROOT}/src/common/version.h.in
                   ${CMAKE_CURRENT_BINARY_DIR}/generated/${_pedal}/common/version.h @ONLY)

    # No PACKAGE_NAME: the bundle folder then takes the TARGET name, which is what
    # module_win32.cpp's validateBundleStructure requires the inner binary to match. A package
    # named "Rations Boost" would give RationsBoost.vst3 inside "Rations Boost.vst3", which does
    # not load on Windows. The name a host DISPLAYS comes from stringPluginName in version.h and
    # is free of this constraint.
    smtg_add_vst3plugin(${_target}
        SOURCES_LIST
            ${RPEDALS_ROOT}/src/plugins/${_pedal}/factory.cpp
            ${RPEDALS_ROOT}/src/common/midilearn.cpp
            ${RPEDALS_ROOT}/src/common/pedalface.cpp
            ${RPEDALS_PLATFORM_VIEW_SOURCES}
    )

    # Put the Windows DLL inside its own bundle.
    #
    # smtg_target_make_plugin_package() places it with a foreach over CMAKE_CONFIGURATION_TYPES,
    # which is EMPTY for every single-config generator — so under Ninja the loop body never runs,
    # the DLL lands at the top of the build tree, and the result looks like a successful build and
    # is not loadable. The path is read back from the properties the SDK itself set rather than
    # spelled out, so it cannot drift from the layout module_win32.cpp's loader expects.
    #
    # NAME: validateBundleStructure requires the inner DLL to be named exactly like the bundle
    # folder — RationsBoost.vst3/Contents/x86_64-win/RationsBoost.vst3. A .dll in the right folder
    # does not load.
    if(RPEDALS_WINDOWS)
        get_target_property(_pkg_path ${_target} SMTG_PLUGIN_PACKAGE_PATH)
        get_target_property(_pkg_contents ${_target} SMTG_PLUGIN_PACKAGE_CONTENTS)
        get_target_property(_win_arch ${_target} SMTG_WIN_ARCHITECTURE_NAME)
        # smtg_target_set_vst_win_architecture_name() derives this from CMAKE_GENERATOR_PLATFORM,
        # a Visual Studio concept that is unset under Ninja + MinGW; it then falls into its else()
        # and yields x86_64-win by accident rather than by decision. Asserted so a future SDK
        # change cannot silently move the bundle out from under the resource path resolver, which
        # walks up two levels from the loaded module.
        if(NOT _win_arch STREQUAL "x86_64-win")
            message(FATAL_ERROR
                "[RationsPedals] the SDK chose architecture folder '${_win_arch}', expected "
                "'x86_64-win'. The bundle layout and the resource path resolver both assume the "
                "latter.")
        endif()
        set_target_properties(${_target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${_pkg_path}/${_pkg_contents}/${_win_arch}")
    endif()

    smtg_target_configure_version_file(${_target})

    # Resources, copied by hand rather than through smtg_target_add_plugin_resources().
    #
    # THE SDK HELPER CANNOT SERVE FIVE PLUG-INS THAT SHARE ART. It ends in
    #     add_custom_command(OUTPUT <in the bundle> MAIN_DEPENDENCY <the source file> ...)
    # plus target_sources(), and MAIN_DEPENDENCY binds a source file to ONE rule per directory
    # scope. Every one of these files is listed by all five targets, so the first target to ask
    # claims it and the other four silently get nothing. Measured, not deduced: with the helper in
    # place, RationsBoost.vst3 carried all eight images and both fonts, and the other four bundles
    # carried only their own enclosure — an editor with no dial, no lamp, no footswitch and a
    # system fallback font, which still built, still validated and still loaded.
    #
    # A POST_BUILD step has no such binding. The bundle path is still read back from the SDK's own
    # target properties, so the layout cannot drift from what the loader expects.
    get_target_property(_res_pkg ${_target} SMTG_PLUGIN_PACKAGE_PATH)
    get_target_property(_res_dir ${_target} SMTG_PLUGIN_PACKAGE_RESOURCES)
    set(_res_root "${_res_pkg}/${_res_dir}")
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_res_root}/fonts" "${_res_root}/img"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RPEDALS_FONT_PATHS} "${_res_root}/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${RPEDALS_SHARED_IMG_PATHS}
                "${RPEDALS_ROOT}/resources/img/pedal-${_pedal}.png"
                "${_res_root}/img"
        VERBATIM
        COMMENT "[RationsPedals] staging ${_pedal} resources into the bundle"
    )

    target_compile_features(${_target} PUBLIC cxx_std_17)
    target_compile_options(${_target} PRIVATE -ffast-math -Wall)
    target_include_directories(${_target} PRIVATE
        ${RPEDALS_ROOT}/src
        ${RPEDALS_ROOT}/deps
        ${RPEDALS_ROOT}/src/plugins/${_pedal}
        ${CMAKE_CURRENT_BINARY_DIR}/generated/${_pedal}
    )
    target_link_libraries(${_target} PRIVATE sdk PedalsGfx ${RPEDALS_PLATFORM_GUI_LIBS})
    # Keeps the SDK's ~800 symbols, and everything else pulled in from a static archive, out of
    # the bundle's dynamic symbol table. With five sibling plug-ins in one host this is the other
    # half of the visibility rule above.
    target_link_options(${_target} PRIVATE -Wl,--exclude-libs,ALL)
    # ... and the version script closes what neither of those reaches: libstdc++ declares
    # `namespace std` with default visibility, so implicitly instantiated templates are exported
    # WEAK whatever -fvisibility says, and with five bundles in one host the first one loaded
    # would win those definitions for all five. See cmake/plugin-exports.map. ELF only; the
    # Windows link is gated on its own export list by makedist-windows.sh.
    if(NOT RPEDALS_WINDOWS)
        target_link_options(${_target} PRIVATE
            "-Wl,--version-script=${RPEDALS_ROOT}/cmake/plugin-exports.map")
        set_property(TARGET ${_target} APPEND PROPERTY
            LINK_DEPENDS "${RPEDALS_ROOT}/cmake/plugin-exports.map")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# The offline proofs and the render tool, behind RPEDALS_BUILD_TOOLS. See the option at the top
# of this file: a parent project that embeds this tree has a `panelrender` target of its own, and
# these are this project's gates rather than its product.
# ---------------------------------------------------------------------------
if(RPEDALS_BUILD_TOOLS)
    # ---------------------------------------------------------------------------
    # loadall — the five bundles in ONE process, which is the situation the visibility rules in this
    # project exist for and the one thing no per-bundle check can see. It is a host, so it loads the
    # built bundles rather than linking the plug-in code, and it needs the SDK's module loader for
    # whichever platform it is built on. See tools/loadall.cpp.
    # ---------------------------------------------------------------------------
    if(RPEDALS_WINDOWS)
        set(RPEDALS_HOST_MODULE_LOADER
            ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
    else()
        set(RPEDALS_HOST_MODULE_LOADER
            ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)
    endif()

    add_executable(loadall
        ${RPEDALS_ROOT}/tools/loadall.cpp
        ${RPEDALS_HOST_MODULE_LOADER}
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
        # The footswitch is switched on through a real input parameter queue, which is the only route
        # a plug-in has to accept. Not in sdk_hosting.
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp
        ${VST3_SDK_DIR}/public.sdk/samples/vst-hosting/editorhost/source/usediids.cpp
    )
    target_include_directories(loadall PRIVATE ${RPEDALS_ROOT}/src ${VST3_SDK_DIR})
    target_link_libraries(loadall PRIVATE sdk_hosting)
    target_compile_features(loadall PRIVATE cxx_std_17)
    target_compile_options(loadall PRIVATE -Wall -Wextra)
    # Nothing to load until they are built.
    foreach(_pedal IN LISTS RPEDALS_PEDALS)
        add_dependencies(loadall ${RPEDALS_TARGET_${_pedal}})
    endforeach()

    # ---------------------------------------------------------------------------
    # panelrender — the offline face renderer and the geometry audit. Links PedalsGfx and nothing
    # else: no VST3, no X11, no Win32, which is what lets it be the first thing built and run when a
    # Windows toolchain is being brought up, and what lets scripts/makedist-windows.sh diff its output
    # against the native build's. See tools/panelrender.cpp.
    # ---------------------------------------------------------------------------
    add_executable(panelrender
        ${RPEDALS_ROOT}/tools/panelrender.cpp
        ${RPEDALS_ROOT}/src/common/pedalface.cpp
        # ...and the description layer, so the strip's binding text is measured against the real font
        # like every other string. It brings in pluginterfaces' typedefs and nothing else.
        ${RPEDALS_ROOT}/src/common/midilearn.cpp
    )
    target_include_directories(panelrender PRIVATE
        ${RPEDALS_ROOT}/src ${RPEDALS_ROOT}/deps ${RPEDALS_ROOT}/vst3sdk)
    target_link_libraries(panelrender PRIVATE PedalsGfx pluginterfaces)
    target_compile_features(panelrender PUBLIC cxx_std_17)
    target_compile_options(panelrender PRIVATE -Wall -Wextra)

    # ---------------------------------------------------------------------------
    # pedalcheck — the DSP proofs. Links the pedal headers directly: no VST3, no host, no audio
    # device, so it runs anywhere the compiler does. See tools/pedalcheck.cpp for what the two kinds
    # of check are for.
    # ---------------------------------------------------------------------------
    add_executable(pedalcheck ${RPEDALS_ROOT}/tools/pedalcheck.cpp)
    target_include_directories(pedalcheck PRIVATE
        ${RPEDALS_ROOT}/src ${RPEDALS_ROOT}/deps ${RPEDALS_ROOT}/vst3sdk)
    target_compile_features(pedalcheck PUBLIC cxx_std_17)
    # -ffast-math, and it is NOT optional: the golden hashes were recorded from a build that had it,
    # and the plug-ins are compiled with it. A check built without it would be checking different
    # arithmetic from the one that ships.
    target_compile_options(pedalcheck PRIVATE -ffast-math -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# The five JACK standalones — each pedal playable without a DAW.
#
# ONE IMPLEMENTATION, FIVE BINARIES. standalone/main.cpp is compiled once per pedal with three
# defines naming the executable, the window title and the bundle to load; nothing in the source
# branches on which pedal it is. Five copies of a host that differed only in a window title would
# drift apart exactly the way five copies of the processor would.
#
# Each is a HOST: it loads the bundle as a module rather than linking the plug-in in, so the
# editor's dladdr-based resource lookup (src/platform/respath.cpp) resolves Contents/Resources
# exactly as it does inside a DAW, and there is no second compilation of the plug-in to keep in
# step with the first.
#
# Linux only, and only when JACK's development headers are present: a standalone is a convenience,
# not the product, and its absence must never stop the plug-ins from building. RPEDALS_BUILD_STANDALONES
# turns them off for a parent project that embeds this tree and ships only the bundles.
# ---------------------------------------------------------------------------
if(RPEDALS_BUILD_STANDALONES AND NOT RPEDALS_WINDOWS)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(RPEDALS_JACK QUIET jack)
    endif()

    if(RPEDALS_JACK_FOUND)
        set(RPEDALS_MODULE_LOADER
            ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)

        foreach(_pedal IN LISTS RPEDALS_PEDALS)
            set(_plugin ${RPEDALS_TARGET_${_pedal}})
            set(_app rations-${_pedal}-standalone)

            add_executable(${_app}
                ${RPEDALS_ROOT}/standalone/main.cpp
                ${RPEDALS_ROOT}/standalone/app.h
                ${RPEDALS_ROOT}/standalone/runloop.h
                ${RPEDALS_ROOT}/standalone/runloop.cpp
                ${RPEDALS_ROOT}/standalone/jackclient.h
                ${RPEDALS_ROOT}/standalone/jackclient.cpp
                ${RPEDALS_ROOT}/standalone/midiroute.h
                ${RPEDALS_ROOT}/standalone/midiroute.cpp
                ${RPEDALS_MODULE_LOADER}
                ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
                # The event list carries Note On to the plug-in — the one of the three footswitch
                # messages that is still MIDI when it arrives. MemoryStream is what the settings
                # file is read and written through. Neither is in sdk_hosting.
                ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/eventlist.cpp
                ${VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp
                ${VST3_SDK_DIR}/public.sdk/samples/vst-hosting/editorhost/source/usediids.cpp
            )
            # Who this binary is. The bundle name is the plug-in target's, so a rename cannot leave
            # a standalone looking for a bundle that is no longer built under that name.
            target_compile_definitions(${_app} PRIVATE
                RPEDALS_APP_EXE="${_app}"
                RPEDALS_APP_TITLE="${RPEDALS_DISPLAY_${_pedal}}"
                RPEDALS_APP_BUNDLE="${_plugin}.vst3"
            )
            target_include_directories(${_app} PRIVATE
                ${RPEDALS_ROOT}/standalone ${RPEDALS_JACK_INCLUDE_DIRS})
            target_link_libraries(${_app} PRIVATE
                sdk_hosting ${RPEDALS_JACK_LIBRARIES} ${RPEDALS_PLATFORM_GUI_LIBS})
            target_link_directories(${_app} PRIVATE ${RPEDALS_JACK_LIBRARY_DIRS})
            target_compile_features(${_app} PRIVATE cxx_std_17)
            target_compile_options(${_app} PRIVATE -Wall -Wextra)
            # The bundle it loads has to exist before it can be run, and it is found beside the
            # binary or in the build tree.
            add_dependencies(${_app} ${_plugin})
        endforeach()
    else()
        message(STATUS "jack not found — the five standalones will not be built "
                       "(the plug-ins are unaffected)")
    endif()
endif()
