cmake_minimum_required(VERSION 3.18)
project(tio VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ---- Qt 6 (MinGW on this machine; adjust CMAKE_PREFIX_PATH to your Qt) ----
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "D:/Qt/6.10.1/mingw_64" ${CMAKE_PREFIX_PATH})
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent PrintSupport)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR "Qt6 not found. Install Qt 6 (Widgets + Network + Concurrent) and set CMAKE_PREFIX_PATH.")
endif()

add_executable(tio WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    SettingsPage.h
    SettingsPage.cpp
    ImageDetailsDialog.h
    ImageDetailsDialog.cpp
    TagFilterDialog.h
    TagFilterDialog.cpp
    LogPanel.h
    LogPanel.cpp
    Logger.h
    Logger.cpp
    LanguageManager.h
    LanguageManager.cpp
    ResultItemWidget.h
    ResultItemWidget.cpp
    LlmClient.h
    LlmClient.cpp
    Spinner.h
    Spinner.cpp
    managers/SettingsManager.h
    managers/SettingsManager.cpp
    managers/ModelManager.h
    managers/ModelManager.cpp
    managers/ExtensionManager.h
    managers/ExtensionManager.cpp
    managers/LibraryManager.h
    managers/LibraryManager.cpp
    managers/CollectionManager.h
    managers/CollectionManager.cpp
    managers/SmartCollectionManager.h
    managers/SmartCollectionManager.cpp
    managers/ClusterNameMapping.h
    managers/ClusterNameMapping.cpp
    ExportReportDialog.h
    ExportReportDialog.cpp
    BatchEditDialog.h
    BatchEditDialog.cpp
    app.rc
    resources.qrc
)

target_link_libraries(tio PRIVATE Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::PrintSupport)

# The GUI drives the console engine (../dsl/build/dsl.exe) via QProcess; no
# ONNX Runtime linkage is needed here.  Copy the engine + its runtime DLLs next
# to the GUI so the co-located copy is runnable.
add_custom_command(TARGET tio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE_DIR:tio>/../../dsl/build/dsl.exe
            $<TARGET_FILE_DIR:tio>/dsl.exe
)
set(_ENGINE_DLLS
    onnxruntime.dll onnxruntime_providers_shared.dll)
foreach(_dll ${_ENGINE_DLLS})
    add_custom_command(TARGET tio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE_DIR:tio>/../../dsl/build/${_dll}
                $<TARGET_FILE_DIR:tio>/${_dll})
endforeach()

# Deploy Qt runtime (DLLs + platform/imagformats plugins) next to the exe so it
# runs when double-clicked (no Qt in PATH).  windeployqt ships with Qt.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS
        "D:/Qt/6.10.1/mingw_64/bin" ENV PATH)
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET tio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E env "PATH=D:/Qt/6.10.1/mingw_64/bin;D:/Qt/Tools/mingw1310_64/bin;$ENV{PATH}"
                    ${WINDEPLOYQT_EXECUTABLE} --no-translations $<TARGET_FILE:tio>
            COMMENT "Deploying Qt runtime next to tio.exe"
        )
        # Qt6Concurrent is a direct dependency not always detected by windeployqt
        add_custom_command(TARGET tio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "D:/Qt/6.10.1/mingw_64/bin/Qt6Concurrent.dll"
                    $<TARGET_FILE_DIR:tio>/Qt6Concurrent.dll
        )
    endif()
endif()

message(STATUS "Qt6: ${Qt6_VERSION}")
message(STATUS "Build complete. Run: gui/build/tio.exe")