cmake_minimum_required(VERSION 3.22)
project(omaphoto VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(OMAPHOTO_WERROR "Treat warnings as errors" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent Test)
find_package(libheif REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(onnxruntime REQUIRED)

add_library(compositor_core STATIC
    src/Logging.cpp
    src/ContentView.cpp
    src/ContentView+Status.cpp
    src/ContentView+Panels.cpp
    src/Document/BrushStroke.cpp
    src/Document/BrushStroke+Stroke.cpp
    src/Document/BrushStroke+Commit.cpp
    src/Document/CanvasSize.cpp
    src/Document/Distort.cpp
    src/Document/DocumentHistory.cpp
    src/Document/EditorSession.cpp
    src/Document/EditorSession+Brush.cpp
    src/Document/EditorSession+Imports.cpp
    src/Document/EditorSession+Layers.cpp
    src/Document/EditorSession+Model.cpp
    src/Document/EditorSession+Projects.cpp
    src/Document/EditorSession+Transform.cpp
    src/Document/LayerAdjustment.cpp
    src/Document/LayerEffects.cpp
    src/Document/LayerEffects+Session.cpp
    src/Document/LayerEffects+Renderer.cpp
    src/Document/AdjustmentEditing.cpp
    src/Document/LayerAppearance.cpp
    src/Document/LayerGroups.cpp
    src/Document/LayerMask.cpp
    src/Document/LayerTransform.cpp
    src/Document/LiveLayerMask.cpp
    src/Document/PixelAdjust.cpp
    src/Document/ProjectWorkspace.cpp
    src/Document/SelectionClipboard.cpp
    src/Document/Crop.cpp
    src/Document/Filters.cpp
    src/Document/GuidedMatte.cpp
    src/Document/SubjectRemoval.cpp
    src/Document/FloatingSelection.cpp
    src/Document/Gradient.cpp
    src/Document/Filters+Session.cpp
    src/Document/Levels.cpp
    src/Document/LevelsAutomatic.cpp
    src/Document/Curves.cpp
    src/Document/ImageAdjustments.cpp
    src/Document/HueSaturation.cpp
    src/Document/HueSaturation+Filter.cpp
    src/Document/HueSaturation+Session.cpp
    src/Document/BlurTool.cpp
    src/Document/CloneStamp.cpp
    src/Document/ColorPalette.cpp
    src/Document/ContentFill.cpp
    src/Document/LayerFlip.cpp
    src/Document/LayerMerge.cpp
    src/Document/MagicWand.cpp
    src/Document/MaskTracing.cpp
    src/Document/PixelInvert.cpp
    src/Document/Selection.cpp
    src/Document/SelectionEdits.cpp
    src/Document/ShapeTool.cpp
    src/Document/TypeTool.cpp
    src/Document/SmudgeLiquify.cpp
    src/UI/BlendModePicker.cpp
    src/UI/BrushControls.cpp
    src/UI/CropControls.cpp
    src/UI/CanvasSizeSheet.cpp
    src/UI/ImageSizeSheet.cpp
    src/UI/JPEGExportSheet.cpp
    src/UI/ByteCounts.cpp
    src/UI/SliderSnap.cpp
    src/UI/GradientControls.cpp
    src/UI/ShapeControls.cpp
    src/UI/TypeControls.cpp
    src/UI/CanvasThumbnail.cpp
    src/UI/ColorPaletteControls.cpp
    src/UI/ColorPickerSheet.cpp
    src/UI/CompositorMenus.cpp
    src/UI/LayerAppearanceControls.cpp
    src/UI/LayerIcons.cpp
    src/UI/LassoControls.cpp
    src/UI/LayerMaskMenu.cpp
    src/UI/LayersPanel.cpp
    src/UI/LevelsSheet.cpp
    src/UI/HueSaturationSheet.cpp
    src/UI/CurvesControls.cpp
    src/UI/NativeLayerList.cpp
    src/UI/NativeLayerList+Buttons.cpp
    src/UI/NativeLayerList+Cell.cpp
    src/UI/NativeLayerList+Drag.cpp
    src/UI/NativeLayerList+Effects.cpp
    src/UI/NavigationToolHeader.cpp
    src/UI/TransformInspector.cpp
    src/UI/EffectsSheet.cpp
    src/UI/FilterSheet.cpp
    src/UI/FloatingPanel.cpp
    src/UI/NewCanvasSheet.cpp
    src/UI/OmarchyTheme.cpp
    src/UI/ProjectTabs.cpp
    src/UI/ProjectWorkspaceView.cpp
    src/UI/ToolHeaderStyle.cpp
    src/UI/ToolIcons.cpp
    src/IO/CanvasResizer.cpp
    src/IO/ImageExporter.cpp
    src/IO/ImageImporter.cpp
    src/IO/ImageResizer.cpp
    src/IO/ProjectController.cpp
    src/IO/ProjectController+Resize.cpp
    src/IO/ProjectController+Export.cpp
    src/IO/ImageFileDrop.cpp
    src/IO/ProjectStore.cpp
    src/IO/ProjectStore+Adjustment.cpp
    src/IO/ProjectStore+Effects.cpp
    src/IO/ProjectStore+Manifest.cpp
    src/Rendering/AdjustmentSurface.cpp
    src/Rendering/CanvasViewport.cpp
    src/Rendering/DownsampleCache.cpp
    src/Rendering/EffectsPreviewCache.cpp
    src/Rendering/EditorCanvas.cpp
    src/Rendering/EditorCanvas+Brush.cpp
    src/Rendering/EditorCanvas+Crop.cpp
    src/Rendering/EditorCanvas+Effects.cpp
    src/Rendering/EditorCanvas+Gradient.cpp
    src/Rendering/EditorCanvas+Cursors.cpp
    src/Rendering/BrushCursorOverlay.cpp
    src/Rendering/EditorCanvas+Navigation.cpp
    src/Rendering/EditorCanvas+Picking.cpp
    src/Rendering/EditorCanvas+Selection.cpp
    src/Rendering/EditorCanvas+Shape.cpp
    src/Rendering/InlineTextEditor.cpp
    src/Rendering/InlineTextEditor+Editing.cpp
    src/Rendering/InlineTextEditor+Input.cpp
    src/Rendering/InlineTextEditor+Pointer.cpp
    src/Rendering/TextLayout.cpp
    src/Rendering/EditorCanvas+Transform.cpp
    src/Rendering/SelectionIcons.cpp
    src/Rendering/EyedropperIcon.cpp
    src/Rendering/SampleRingOverlay.cpp
    src/Rendering/TransformOverlay.cpp
    src/Rendering/HslBlend.cpp
    src/Rendering/LayerEffectsKernel.cpp
    src/Rendering/LayerEffectsSurface.cpp
    src/Rendering/LayerRenderer.cpp
    src/Rendering/LiveMaskRenderer.cpp
    src/Rendering/BrushCoverage.cpp
    src/Rendering/RasterSnapshot.cpp
    src/Rendering/TiledLayerRenderer.cpp
)
# Shared macOS pixel kernels, always optimized.
set(OMAPHOTO_KERNELS ${CMAKE_CURRENT_SOURCE_DIR}/kernels)
target_sources(compositor_core PRIVATE ${OMAPHOTO_KERNELS}/AdjustPixels.c ${OMAPHOTO_KERNELS}/BrushPixels.c ${OMAPHOTO_KERNELS}/WandPixels.c ${OMAPHOTO_KERNELS}/ContentFill.c
    ${OMAPHOTO_KERNELS}/HealPixels.c ${OMAPHOTO_KERNELS}/LevelsPixels.c ${OMAPHOTO_KERNELS}/NoisePixels.c ${OMAPHOTO_KERNELS}/LensPixels.c)
set_source_files_properties(${OMAPHOTO_KERNELS}/AdjustPixels.c ${OMAPHOTO_KERNELS}/BrushPixels.c ${OMAPHOTO_KERNELS}/WandPixels.c ${OMAPHOTO_KERNELS}/ContentFill.c
    ${OMAPHOTO_KERNELS}/HealPixels.c ${OMAPHOTO_KERNELS}/LevelsPixels.c ${OMAPHOTO_KERNELS}/NoisePixels.c ${OMAPHOTO_KERNELS}/LensPixels.c
    PROPERTIES COMPILE_OPTIONS -O2)
# The per-pixel loops vectorise only at -O3.
set_source_files_properties(src/Rendering/BrushCoverage.cpp src/Rendering/DownsampleCache.cpp PROPERTIES COMPILE_OPTIONS -O3)
target_include_directories(compositor_core PUBLIC src PRIVATE ${OMAPHOTO_KERNELS})
target_link_libraries(compositor_core PUBLIC Qt6::Widgets Qt6::Concurrent PRIVATE heif Fontconfig::Fontconfig onnxruntime::onnxruntime)
target_compile_options(compositor_core PUBLIC -Wall -Wextra
    $<$<BOOL:${OMAPHOTO_WERROR}>:-Werror>)
# Empty optionals and bad indexes abort, not pass by luck.
target_compile_definitions(compositor_core PUBLIC
    OMAPHOTO_VERSION="${PROJECT_VERSION}" _GLIBCXX_ASSERTIONS)

add_executable(omaphoto src/CompositorApp.cpp)
target_link_libraries(omaphoto PRIVATE compositor_core)
include(GNUInstallDirs)
install(TARGETS omaphoto RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.github.ZacharyZhang_NY.OmaPhoto.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/io.github.ZacharyZhang_NY.OmaPhoto.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
# Swift's app icon, one size a hicolor folder.
foreach(size 16 32 64 128 256 512)
    install(FILES packaging/icons/app-icon-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME io.github.ZacharyZhang_NY.OmaPhoto.png)
endforeach()
install(FILES licenses/U-2-Net.txt LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/omaphoto)
# A direct install registers the project type; packages use hooks.
install(CODE "
    if(\"\$ENV{DESTDIR}\" STREQUAL \"\")
        find_program(UPDATE_MIME update-mime-database REQUIRED)
        execute_process(COMMAND \${UPDATE_MIME} \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/mime\" COMMAND_ERROR_IS_FATAL ANY)
    endif()")
# Packages carry their ONNX Runtime; each release renames its symbols.
option(OMAPHOTO_BUNDLE_ORT "Install ONNX Runtime beside the app" OFF)
set(OMAPHOTO_ORT_NOTICES /usr/local/share/doc/onnxruntime CACHE PATH "ONNX Runtime's LICENSE and notices")
if(OMAPHOTO_BUNDLE_ORT)
    # Microsoft's licence travels with its library.
    foreach(notice LICENSE ThirdPartyNotices.txt)
        if(NOT EXISTS ${OMAPHOTO_ORT_NOTICES}/${notice})
            message(FATAL_ERROR "bundling ONNX Runtime needs ${OMAPHOTO_ORT_NOTICES}/${notice}")
        endif()
        install(FILES ${OMAPHOTO_ORT_NOTICES}/${notice} DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/omaphoto/onnxruntime)
    endforeach()
    install(IMPORTED_RUNTIME_ARTIFACTS onnxruntime::onnxruntime LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/omaphoto)
    set_target_properties(omaphoto PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/omaphoto")
    # rpmbuild would offer the bundled library to the system.
    set(CPACK_RPM_SPEC_MORE_DEFINE "%global __provides_exclude_from ^%{_libdir}/omaphoto/.*$\n%global __requires_exclude ^libonnxruntime[.]so")
endif()
# Debian and Fedora packages; each built on its own system.
set(CPACK_PACKAGE_NAME omaphoto)
set(CPACK_PACKAGE_VENDOR "ZacharyZhang-NY")
set(CPACK_PACKAGE_CONTACT "ZacharyZhang-NY")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Layered image editing for Linux, a port of Compositor")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/ZacharyZhang-NY/OmaPhoto")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION graphics)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# What no library names: HEVC decoding, TIFF, the MIME tools.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libheif-plugin-libde265, qt6-image-formats-plugins, shared-mime-info, hicolor-icon-theme")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE MIT)
set(CPACK_RPM_PACKAGE_GROUP Applications/Multimedia)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtimageformats, shared-mime-info, hicolor-icon-theme")
# Fedora decodes HEVC through RPM Fusion's libheif-freeworld.
set(CPACK_RPM_PACKAGE_SUGGESTS libheif-freeworld)
include(CPack)
# Remove Background's model, which packages fetch by checksum.
set(OMAPHOTO_MODEL "" CACHE FILEPATH "u2net.onnx to install")
if(OMAPHOTO_MODEL)
    # The file itself: a link leads back to this machine.
    file(REAL_PATH "${OMAPHOTO_MODEL}" model)
    install(FILES ${model} DESTINATION ${CMAKE_INSTALL_DATADIR}/omaphoto RENAME u2net.onnx)
endif()

enable_testing()
function(compositor_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE compositor_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

compositor_test(AdjustmentSurfaceTests)
compositor_test(BlendModePickerTests)
compositor_test(BlendShortcutTests)
compositor_test(LayersPanelTests)
compositor_test(LayerListFocusTests)
compositor_test(LayerDragTests)
compositor_test(LayerDragGestureTests)
compositor_test(ProjectDropTests)
compositor_test(BlendModeTests)
compositor_test(BrushCanvasTests)
compositor_test(BrushGestureTests)
compositor_test(BrushKeyTests)
compositor_test(BrushCursorOverlayTests)
compositor_test(BrushControlsTests)
compositor_test(BrushPerformanceTests)
compositor_test(CanvasPerformanceTests)
compositor_test(PoolMapTests)
compositor_test(DesktopIntegrationTests)
# The install it checks needs the app built.
add_dependencies(DesktopIntegrationTests omaphoto)
# What this build installs, as CMake itself reads it.
target_compile_definitions(DesktopIntegrationTests PRIVATE OMAPHOTO_BUNDLED=$<BOOL:${OMAPHOTO_BUNDLE_ORT}>
                           OMAPHOTO_LIBDIR="${CMAKE_INSTALL_LIBDIR}" OMAPHOTO_MODEL_PATH="${OMAPHOTO_MODEL}")
compositor_test(BrushIntersectionTests)
compositor_test(BrushSessionTests)
compositor_test(BrushPreviewTests)
compositor_test(BrushRasterTests)
compositor_test(BrushTests)
compositor_test(BrushCoverageTests)
compositor_test(SpotHealingTests)
compositor_test(CloneStampTests)
compositor_test(CloneStampCanvasTests)
compositor_test(SmudgeLiquifyTests)
compositor_test(BlurToolTests)
compositor_test(SmearCanvasTests)
compositor_test(GradientTests)
compositor_test(GradientCanvasTests)
compositor_test(ShapeToolTests)
compositor_test(ShapeLayerTests)
compositor_test(ShapeCanvasTests)
compositor_test(TypeToolTests)
compositor_test(TextLayoutTests)
compositor_test(TypeSessionTests)
compositor_test(TypeStoreTests)
compositor_test(TypeControlsTests)
compositor_test(TypeFontPickerTests)
compositor_test(InlineTextEditorTests)
compositor_test(InlineTextInputTests)
compositor_test(InlineTextDrawTests)
compositor_test(InlineTextPreeditTests)
compositor_test(InlineTextPointerTests)
compositor_test(InlineTextHandleTests)
compositor_test(ColorPickerTests)
compositor_test(FloatingPanelTests)
compositor_test(ColorSwatchTests)
compositor_test(ColorPaletteControlsTests)
compositor_test(ColorPickerSheetTests)
compositor_test(EyedropperTests)
compositor_test(PickingTests)
compositor_test(LevelsTests)
compositor_test(LevelsSessionTests)
compositor_test(LevelsModelTests)
compositor_test(LevelsEdgeTests)
compositor_test(LevelsFailureTests)
compositor_test(LevelsSheetTests)
compositor_test(LevelsHandleTests)
compositor_test(LevelsCanvasTests)
compositor_test(ImageAdjustmentTests)
compositor_test(ImageAdjustmentModelTests)
compositor_test(CurvesControlsTests)
compositor_test(HueSaturationTests)
compositor_test(HueSaturationModelTests)
compositor_test(HueSaturationSessionTests)
compositor_test(HueSaturationFailureTests)
compositor_test(HueSaturationSheetTests)
compositor_test(HueSaturationSpectrumTests)
compositor_test(HueSaturationCanvasTests)
compositor_test(CanvasDisplayTests)
compositor_test(CanvasNavigationTests)
compositor_test(CanvasSizeTests)
compositor_test(CanvasThumbnailTests)
compositor_test(ClippingStackTests)
compositor_test(CompositorTests)
compositor_test(CompositorMenusTests)
compositor_test(CompositorUITestsLaunchTests)
compositor_test(ContentViewTests)
compositor_test(ContentViewStatusTests)
compositor_test(DistortTests)
compositor_test(DownsampleTests)
compositor_test(DrawCoverageTests)
compositor_test(EditorSessionSignalTests)
compositor_test(EditorSessionTests)
compositor_test(ExportTests)
compositor_test(FolderMaskClipTests)
compositor_test(EditorCanvasTests)
compositor_test(GroupSessionTests)
compositor_test(GroupTests)
compositor_test(GroupingSelectionTests)
compositor_test(HighDpiCanvasTests)
compositor_test(HistoryTests)
compositor_test(ImageImportSessionTests)
compositor_test(ImageImportTests)
compositor_test(ImageSizeTests)
compositor_test(JPEGExportTests)
compositor_test(LayerAppearanceControlsTests)
compositor_test(LayerAppearanceTests)
compositor_test(LayerMaskEditTests)
compositor_test(LayerMaskSessionTests)
compositor_test(LayerMaskTests)
compositor_test(LayerRendererTests)
compositor_test(LayerTests)
compositor_test(LiveMaskRendererTests)
compositor_test(LiveMaskBakeTests)
compositor_test(LiveMaskDeleteTests)
compositor_test(LiveMaskEditTests)
compositor_test(LiveMaskSessionTests)
compositor_test(LiveMaskTests)
compositor_test(MaskPlacementTests)
compositor_test(MaskSignalTests)
compositor_test(MaskTracingTests)
compositor_test(MaskTransformTests)
compositor_test(ProjectManifestTests)
compositor_test(NativeLayerListTests)
compositor_test(NavigationToolHeaderTests)
compositor_test(NewCanvasSheetTests)
compositor_test(OmarchyThemeTests)
compositor_test(ProjectControllerTests)
compositor_test(ProjectCopyTests)
compositor_test(ProjectOpenTests)
compositor_test(ProjectReceiveTests)
compositor_test(ProjectReplaceTests)
compositor_test(ProjectWorkspaceFlowTests)
compositor_test(ProjectWorkspaceSignalTests)
compositor_test(ProjectWorkspaceTests)
compositor_test(ProjectWorkspaceViewTests)
# It starts the real app beside itself.
add_dependencies(ProjectWorkspaceViewTests omaphoto)
compositor_test(ProjectSessionTests)
compositor_test(ProjectTests)
compositor_test(RasterSnapshotSessionTests)
compositor_test(RasterSnapshotTests)
compositor_test(SessionGateTests)
compositor_test(TiledLayerTests)
compositor_test(TiledPieceTests)
compositor_test(TiledStrokeTests)
compositor_test(TransformSessionTests)
compositor_test(ToolIconTests)
compositor_test(TransformTests)
compositor_test(TransformOverlayTests)
compositor_test(TransformPressTests)
compositor_test(TransformInspectorTests)
compositor_test(TransformSnapTests)
compositor_test(CursorTests)
compositor_test(DistortCanvasTests)
compositor_test(DistortMaskTests)
compositor_test(FiltersTests)
compositor_test(FilterTests)
compositor_test(FilterModelTests)
# It checks what the filters hand Swift's C kernels.
target_include_directories(FilterModelTests PRIVATE ${OMAPHOTO_KERNELS})
compositor_test(FilterSessionTests)
compositor_test(FilterFailureTests)
compositor_test(LayerAdjustmentTests)
compositor_test(AdjustmentLayerTests)
compositor_test(AdjustmentEditorTests)
compositor_test(AdjustmentPanelTests)
compositor_test(LayerEffectsTests)
compositor_test(LayerEffectsSessionTests)
compositor_test(LayerEffectsKernelTests)
compositor_test(EffectsPreviewCacheTests)
compositor_test(LayerEffectsSurfaceTests)
compositor_test(EffectsCanvasTests)
compositor_test(EffectsFailureTests)
compositor_test(EffectsPanelTests)
compositor_test(LayerEffectRowTests)
compositor_test(GuidedMatteTests)
compositor_test(SubjectRemovalTests)
compositor_test(SubjectRemovalModelTests)
compositor_test(SubjectRemovalFailureTests)
compositor_test(RemoveBackgroundTests)
compositor_test(SelectSubjectTests)
compositor_test(LayerEffectsRendererTests)
compositor_test(LayerFlipTests)
compositor_test(LayerMergeTests)
compositor_test(PixelEditTests)
compositor_test(PixelInvertTests)
compositor_test(PixelMoveTests)
compositor_test(SelectionTests)
compositor_test(SelectionFeatherTests)
compositor_test(SelectionSourceTests)
compositor_test(SelectionEditTests)
compositor_test(SelectionSignalTests)
compositor_test(SelectMenuTests)
compositor_test(SelectionCanvasTests)
compositor_test(SelectionOverlayTests)
compositor_test(LassoControlsTests)
compositor_test(SelectionAmountSheetTests)
compositor_test(MagicWandTests)
compositor_test(SelectionClipboardTests)
compositor_test(SmartEditTests)
compositor_test(FilterSheetTests)
compositor_test(FilterControlsTests)
compositor_test(FloatingSelectionTests)
compositor_test(CropTests)
compositor_test(CropSessionTests)
compositor_test(CropCanvasTests)
compositor_test(CropControlsTests)
compositor_test(CropFailureTests)
compositor_test(CanvasSizeSheetTests)
compositor_test(ImageSizeSheetTests)
compositor_test(ProjectResizeTests)
compositor_test(JPEGExportSheetTests)
compositor_test(ProjectExportTests)
compositor_test(ProjectExportFailureTests)
compositor_test(ByteCountsTests)
compositor_test(ImageFileDropTests)
compositor_test(SliderSnapTests)
compositor_test(ProjectResizeFailureTests)
