# SPDX-License-Identifier: GPL-2.0-only
cmake_minimum_required(VERSION 3.21)

project(Talos
    VERSION 0.3.0
    DESCRIPTION "Talos, a desktop and mobile client for the Talon agent"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# GitHub "owner/repo" whose releases the in-app updater watches. Empty
# disables the check until the user sets one in Settings.
set(TALOS_UPDATE_REPO "" CACHE STRING "GitHub owner/repo for client updates")
option(TALOS_BUILD_TESTS "Build the unit tests" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Network Concurrent Qml Quick QuickControls2 Widgets)
find_package(Qt6 6.8 QUIET OPTIONAL_COMPONENTS
    Multimedia TextToSpeech Positioning DBus Test)

qt_standard_project_setup(REQUIRES 6.8)

# ── Vendored crypto (Argon2id + XChaCha20-Poly1305 for the app lock) ────────
add_library(monocypher STATIC third_party/monocypher/monocypher.c)
target_include_directories(monocypher PUBLIC third_party/monocypher)
set_target_properties(monocypher PROPERTIES C_STANDARD 99 AUTOMOC OFF)

# ── Core: protocol, state, mesh. No QML, so the tests link it directly. ─────
qt_add_library(talos_core STATIC
    src/core/Profile.h src/core/Profile.cpp
    src/core/SseParser.h
    src/core/BridgeClient.h src/core/BridgeClient.cpp
    src/core/Models.h src/core/Models.cpp
    src/core/Markdown.h src/core/Markdown.cpp
    src/core/Store.h src/core/Store.cpp
    src/core/SecretStore.h src/core/SecretStore.cpp
    src/core/Session.h src/core/Session.cpp
    src/core/MediaStore.h src/core/MediaStore.cpp
    src/core/BridgeLinks.h src/core/BridgeLinks.cpp
    src/core/AppLog.h src/core/AppLog.cpp
    src/core/Files.h src/core/Files.cpp
    src/core/Math.h src/core/Math.cpp
    src/core/Flow.h src/core/Flow.cpp
    src/core/Crypto.h src/core/Crypto.cpp
    src/mesh/DeviceExec.h src/mesh/DeviceExec.cpp
    src/mesh/MeshService.h src/mesh/MeshService.cpp
    src/mesh/Platform.h src/mesh/Platform.cpp
    src/mesh/SelfUpdate.h src/mesh/SelfUpdate.cpp
    src/mesh/ServiceInstall.h src/mesh/ServiceInstall.cpp
    src/mesh/Headless.h src/mesh/Headless.cpp
    src/mesh/ScreenControl.h src/mesh/ScreenControl.cpp
)
target_include_directories(talos_core PUBLIC src)
target_link_libraries(talos_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent Qt6::Qml
    PRIVATE monocypher)
target_compile_definitions(talos_core PUBLIC
    TALOS_VERSION="${PROJECT_VERSION}"
    TALOS_UPDATE_REPO="${TALOS_UPDATE_REPO}"
    QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
if(TARGET Qt6::Positioning)
    target_link_libraries(talos_core PUBLIC Qt6::Positioning)
    target_compile_definitions(talos_core PUBLIC TALOS_HAVE_POSITIONING)
endif()
if(TARGET Qt6::DBus AND UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(talos_core PUBLIC Qt6::DBus)
    target_compile_definitions(talos_core PUBLIC TALOS_HAVE_DBUS)
endif()
if(APPLE)
    target_link_libraries(talos_core PRIVATE "-framework Security" "-framework CoreFoundation")
endif()
if(WIN32)
    target_link_libraries(talos_core PRIVATE crypt32)
endif()

# ── App ─────────────────────────────────────────────────────────────────────
qt_add_executable(talos
    src/main.cpp
    src/app/App.h src/app/App.cpp
    src/app/AppLock.h src/app/AppLock.cpp
    src/app/Biometric.h src/app/Biometric.cpp
    src/app/Updater.h src/app/Updater.cpp
    src/app/Voice.h src/app/Voice.cpp
    src/app/Desktop.h src/app/Desktop.cpp
    src/app/CodeHighlighter.h src/app/CodeHighlighter.cpp
    src/app/IconItem.h src/app/IconItem.cpp
    src/app/NoNetwork.h src/app/NoNetwork.cpp
    src/app/FlowChartItem.h src/app/FlowChartItem.cpp
    src/app/GlobalShortcut.h src/app/GlobalShortcut.cpp
)

set(TALON_QML_SINGLETONS qml/Theme.qml)
set_source_files_properties(${TALON_QML_SINGLETONS} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
file(GLOB_RECURSE TALON_QML_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml)
foreach(f ${TALON_QML_FILES})
    get_filename_component(alias ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()

qt_add_qml_module(talos
    URI Talos
    VERSION 1.0
    # Under the engine's default import path (qrc:/qt/qml/Talos), so the
    # module is found wherever the program is installed, not only when its
    # build directory happens to sit beside it.
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${TALON_QML_FILES}
)

target_include_directories(talos PRIVATE src)
target_link_libraries(talos PRIVATE
    talos_core Qt6::Quick Qt6::QuickControls2 Qt6::Widgets)
if(ANDROID)
    # QtAndroidPrivate::startActivity, for the system speech recogniser.
    find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
    target_link_libraries(talos PRIVATE Qt6::CorePrivate)

    # Qt has no TLS of its own on Android: it loads these two at run time,
    # and without them the app can neither make an https connection nor open
    # a client certificate. packaging/android/build-openssl.sh builds them.
    set(TALOS_ANDROID_OPENSSL_DIR "${CMAKE_SOURCE_DIR}/build-openssl/${ANDROID_ABI}"
        CACHE PATH "Directory holding libssl_3.so and libcrypto_3.so for the target ABI")
    foreach(lib libcrypto_3.so libssl_3.so)
        if(NOT EXISTS "${TALOS_ANDROID_OPENSSL_DIR}/${lib}")
            message(FATAL_ERROR
                "${lib} is missing from ${TALOS_ANDROID_OPENSSL_DIR}. "
                "Run packaging/android/build-openssl.sh, or set TALOS_ANDROID_OPENSSL_DIR.")
        endif()
        set_property(TARGET talos APPEND PROPERTY
            QT_ANDROID_EXTRA_LIBS "${TALOS_ANDROID_OPENSSL_DIR}/${lib}")
    endforeach()
endif()
# The ring is synthesised and played through Qt Multimedia on desktop;
# Android uses the phone's own ringtone instead.
if(TARGET Qt6::Multimedia AND NOT ANDROID)
    target_link_libraries(talos PRIVATE Qt6::Multimedia)
    target_compile_definitions(talos PRIVATE TALOS_HAVE_MULTIMEDIA)
endif()
if(TARGET Qt6::TextToSpeech)
    target_link_libraries(talos PRIVATE Qt6::TextToSpeech)
    target_compile_definitions(talos PRIVATE TALOS_HAVE_TTS)
endif()

# Translations: compiled into the program, so a package is still one file.
# Without Qt's linguist tools the app simply ships in English.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS LinguistTools)
if(TARGET Qt6::lrelease)
    qt_add_translations(TARGETS talos
        TS_FILES i18n/talos_es.ts i18n/talos_hi.ts
        RESOURCE_PREFIX /i18n
        LUPDATE_OPTIONS -no-obsolete -locations none)
else()
    message(STATUS "Qt linguist tools not found: building without translations")
endif()

# QML is shipped as bytecode, not compiled to C++. The compiled form calls
# into Qt's private API, which ties a binary to one exact Qt build; bytecode
# only needs the public one, so the same package keeps working when the
# distribution updates Qt.
set_target_properties(talos PROPERTIES QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")

set_target_properties(talos PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER dev.talos.client
    MACOSX_BUNDLE_BUNDLE_NAME "Talos"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in
    QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android
    QT_ANDROID_PACKAGE_NAME dev.talos.client
    QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
    QT_ANDROID_VERSION_CODE 6
    QT_ANDROID_TARGET_SDK_VERSION 35
    QT_ANDROID_MIN_SDK_VERSION 28
)

include(GNUInstallDirs)
if(NOT ANDROID)
    install(TARGETS talos
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID)
    install(FILES packaging/linux/dev.talos.client.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/talos.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME dev.talos.client.svg)
endif()

# ── Tests ───────────────────────────────────────────────────────────────────
if(TALOS_BUILD_TESTS AND TARGET Qt6::Test AND NOT ANDROID)
    enable_testing()
    qt_add_executable(talos_tests tests/tst_core.cpp)
    target_link_libraries(talos_tests PRIVATE talos_core Qt6::Test)
    target_compile_definitions(talos_tests PRIVATE
        TALOS_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME talos_tests COMMAND talos_tests)
endif()
