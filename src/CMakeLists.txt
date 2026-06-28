set(MYLR_CORE_SOURCES
    core/DevelopSettings.cpp
    core/EditGraph.cpp
    core/SidecarIO.cpp
    core/PresetManager.cpp
    core/XmpParser.cpp
)

set(MYLR_RAW_SOURCES
    raw/RawDecoder.cpp
)

set(MYLR_RENDER_SOURCES
    render/DevelopPipeline.cpp
    render/StraightenDetector.cpp
)

set(MYLR_LUT_SOURCES
    lut/LutEngine.cpp
    lut/LutImporter.cpp
)

set(MYLR_AI_SOURCES
    ai/FeatureExtractor.cpp
    ai/ClassicalMatcher.cpp
    ai/OnnxRegressor.cpp
    ai/MatchEngine.cpp
    ai/SceneClassifier.cpp
    ai/AutoAdjust.cpp
)

set(MYLR_COLOR_SOURCES
    color/ColorManager.cpp
    color/CameraProfile.cpp
)

set(MYLR_CATALOG_SOURCES
    catalog/Catalog.cpp
)

set(MYLR_EXPORT_SOURCES
    export/ExportEngine.cpp
)

set(MYLR_UI_SOURCES
    ui/HistogramWidget.cpp
    ui/DevelopPanel.cpp
    ui/ImageViewport.cpp
    ui/CropOverlay.cpp
    ui/FilmstripWidget.cpp
    ui/CollapsiblePanel.cpp
    ui/ToneCurveEditor.cpp
    ui/ColorWheelWidget.cpp
    ui/LibraryGridView.cpp
    ui/AICompareView.cpp
)

set(MYLR_APP_SOURCES
    app/main.cpp
    app/DocumentController.cpp
    app/MainWindow.cpp
)

add_library(mylr_lut STATIC ${MYLR_LUT_SOURCES})
target_include_directories(mylr_lut PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mylr_lut PUBLIC Qt6::Core)

add_library(mylr_core STATIC ${MYLR_CORE_SOURCES})
target_include_directories(mylr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mylr_core PUBLIC Qt6::Core Qt6::Gui mylr_lut)

add_library(mylr_raw STATIC ${MYLR_RAW_SOURCES})
target_include_directories(mylr_raw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mylr_raw PUBLIC mylr_core mylr_color Qt6::Core Qt6::Gui)
if(MYLR_USE_LIBRAW AND (libraw_FOUND OR LibRaw_FOUND))
    if(TARGET libraw::raw)
        target_link_libraries(mylr_raw PUBLIC libraw::raw)
    elseif(TARGET LibRaw::LibRaw)
        target_link_libraries(mylr_raw PUBLIC LibRaw::LibRaw)
    endif()
endif()

add_library(mylr_render STATIC ${MYLR_RENDER_SOURCES})
target_include_directories(mylr_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mylr_render PUBLIC mylr_core mylr_lut mylr_color Qt6::Core Qt6::Gui)

add_library(mylr_ai STATIC ${MYLR_AI_SOURCES})
target_include_directories(mylr_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mylr_ai PUBLIC mylr_core mylr_render mylr_raw Qt6::Core Qt6::Gui)
if(MYLR_USE_ONNX AND onnxruntime_FOUND)
    target_link_libraries(mylr_ai PUBLIC onnxruntime::onnxruntime)
endif()

add_library(mylr_color STATIC ${MYLR_COLOR_SOURCES})
target_include_directories(mylr_color PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mylr_color PUBLIC Qt6::Core)

add_library(mylr_catalog STATIC ${MYLR_CATALOG_SOURCES})
target_include_directories(mylr_catalog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mylr_catalog PUBLIC Qt6::Core Qt6::Sql)

add_library(mylr_export STATIC ${MYLR_EXPORT_SOURCES})
target_include_directories(mylr_export PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mylr_export PUBLIC mylr_core mylr_render Qt6::Core Qt6::Gui)

set(MYLR_RESOURCES ${CMAKE_SOURCE_DIR}/resources/app.qrc)
if(WIN32)
    list(APPEND MYLR_RESOURCES ${CMAKE_SOURCE_DIR}/resources/app.rc)
endif()

add_executable(MyLightroom ${MYLR_APP_SOURCES} ${MYLR_UI_SOURCES} ${MYLR_RESOURCES})
target_include_directories(MyLightroom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(MyLightroom PRIVATE
    mylr_core
    mylr_raw
    mylr_render
    mylr_lut
    mylr_ai
    mylr_catalog
    mylr_export
    mylr_color
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Sql
    Qt6::Concurrent
)

if(WIN32)
    set_target_properties(MyLightroom PROPERTIES WIN32_EXECUTABLE TRUE)

    set(_mylr_vcpkg_triplet "${VCPKG_TARGET_TRIPLET}")
    if(NOT _mylr_vcpkg_triplet)
        set(_mylr_vcpkg_triplet "x64-windows")
    endif()
    set(_mylr_qt_plugins "${CMAKE_BINARY_DIR}/vcpkg_installed/${_mylr_vcpkg_triplet}/Qt6/plugins")
    if(EXISTS "${_mylr_qt_plugins}")
        foreach(_mylr_plugin_dir platforms styles imageformats sqldrivers)
            add_custom_command(TARGET MyLightroom POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_mylr_qt_plugins}/${_mylr_plugin_dir}"
                    "$<TARGET_FILE_DIR:MyLightroom>/${_mylr_plugin_dir}"
                COMMENT "Deploying Qt plugin: ${_mylr_plugin_dir}"
            )
        endforeach()
    endif()

    # Copy ALL runtime DLLs from vcpkg's bin directory. This guarantees that the
    # transitive dependencies of the Qt plugins (jpeg62.dll for qjpeg, sqlite3.dll
    # for qsqlite, etc.) and of LibRaw are present, and that the Qt core DLLs match
    # the plugins. Without these, qjpeg/qsqlite fail to load (JPEG decode fails,
    # the catalog DB never opens) and RAW thumbnail decoding can crash.
    set(_mylr_vcpkg_bin "${CMAKE_BINARY_DIR}/vcpkg_installed/${_mylr_vcpkg_triplet}/bin")
    if(EXISTS "${_mylr_vcpkg_bin}")
        add_custom_command(TARGET MyLightroom POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_mylr_vcpkg_bin}"
                "$<TARGET_FILE_DIR:MyLightroom>"
            COMMENT "Deploying vcpkg runtime DLLs (Qt, LibRaw, jpeg, sqlite, ...)"
        )
    endif()
endif()
