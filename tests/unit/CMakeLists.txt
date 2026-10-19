find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test Svg Network DBus)

set(CMAKE_AUTOMOC ON)

# flameshot_add_unit_test(<name> SOURCES <files under src/...> [LIBS <libs>])
# Builds tests/unit/<name>.cpp together with the listed production sources
# and registers it with CTest. Tests link only what they exercise.
function(flameshot_add_unit_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  set(prod_sources "")
  foreach(src IN LISTS ARG_SOURCES)
    list(APPEND prod_sources "${CMAKE_SOURCE_DIR}/src/${src}")
  endforeach()
  add_executable(${name} ${name}.cpp ${prod_sources})
  target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test ${ARG_LIBS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

flameshot_add_unit_test(tst_strfparse SOURCES utils/strfparse.cpp)
flameshot_add_unit_test(tst_savepath SOURCES utils/savepath.cpp)
flameshot_add_unit_test(tst_rulermath SOURCES tools/ruler/rulermath.cpp)
flameshot_add_unit_test(tst_capturefirstrequest SOURCES core/capturefirstrequest.cpp core/capturerequest.cpp)
flameshot_add_unit_test(tst_toaststack SOURCES widgets/toaststack.cpp)
flameshot_add_unit_test(tst_windowpick SOURCES platform/windowpick.cpp)
flameshot_add_unit_test(tst_capturehistory SOURCES utils/capturehistory.cpp)
flameshot_add_unit_test(tst_actionlist SOURCES core/actionlist.cpp)
flameshot_add_unit_test(tst_dockwindowtracker SOURCES core/dockwindowtracker.cpp)
flameshot_add_unit_test(tst_actionrunner SOURCES core/actionrunner.cpp core/actionlist.cpp)
flameshot_add_unit_test(tst_hotkeys SOURCES core/hotkeyutils.cpp)
flameshot_add_unit_test(tst_imageeffects SOURCES utils/imageeffects.cpp utils/beautifylayout.cpp utils/backgroundfx.cpp utils/beautifypalette.cpp utils/autocolors.cpp utils/glassframe.cpp)
flameshot_add_unit_test(tst_glassframe SOURCES utils/glassframe.cpp utils/imageeffects.cpp utils/beautifylayout.cpp utils/backgroundfx.cpp utils/beautifypalette.cpp utils/autocolors.cpp)
flameshot_add_unit_test(tst_beautifylayout SOURCES utils/beautifylayout.cpp utils/imageeffects.cpp utils/backgroundfx.cpp utils/beautifypalette.cpp utils/autocolors.cpp utils/glassframe.cpp)
flameshot_add_unit_test(tst_easing SOURCES utils/easing.cpp)
flameshot_add_unit_test(tst_gifquant SOURCES utils/gifquant.cpp)
flameshot_add_unit_test(tst_recordingstate SOURCES core/recordingstate.cpp)
flameshot_add_unit_test(tst_recordingedit SOURCES core/recordingedit.cpp)
flameshot_add_unit_test(tst_gifplan SOURCES core/gifplan.cpp)
flameshot_add_unit_test(tst_recordinggeometry SOURCES core/recordinggeometry.cpp)
flameshot_add_unit_test(tst_recordingoptions SOURCES core/recordingoptions.cpp)
flameshot_add_unit_test(tst_pickerstyle SOURCES widgets/capture/pickerstyle.cpp core/capturerequest.cpp)
flameshot_add_unit_test(tst_editcanvas SOURCES widgets/editcanvas.cpp)
flameshot_add_unit_test(tst_editorsettings SOURCES widgets/editorsettings.cpp)
flameshot_add_unit_test(tst_curvemath SOURCES tools/curvemath.cpp)
flameshot_add_unit_test(tst_editorvisibility SOURCES widgets/editorvisibility.cpp)
flameshot_add_unit_test(tst_beautifyaction SOURCES tools/beautify/beautifyaction.cpp)
flameshot_add_unit_test(tst_thumbnailgesture SOURCES widgets/thumbnailgesture.cpp)
flameshot_add_unit_test(tst_capturedelay SOURCES core/capturedelay.cpp)
flameshot_add_unit_test(tst_scrollstitch SOURCES utils/scrollstitch.cpp)
flameshot_add_unit_test(tst_outputpath SOURCES utils/outputpath.cpp)
flameshot_add_unit_test(tst_loginitem SOURCES utils/loginitem.cpp)
flameshot_add_unit_test(tst_scrollcapturestate SOURCES core/scrollcapturestate.cpp)
# Every drawable tool, built against an in-memory ConfigHandler
# (fakes/fakeconfighandler.cpp) so tests never read or write the user's
# settings. Shared by the tests that draw real annotations.
set(_drawable_tool_sources
    utils/captureproject.cpp tools/drawabletools.cpp tools/toolstate.cpp
    tools/capturetool.h utils/confighandler.h
    tools/capturecontext.cpp core/capturerequest.cpp
    tools/abstracttwopointtool.cpp tools/abstractpathtool.cpp
    tools/curvemath.cpp tools/ruler/rulermath.cpp
    tools/arrow/arrowtool.cpp tools/circle/circletool.cpp
    tools/circlecount/circlecounttool.cpp tools/invert/inverttool.cpp
    tools/line/linetool.cpp tools/marker/markertool.cpp
    tools/pencil/penciltool.cpp tools/pixelate/pixelatetool.cpp
    tools/rectangle/rectangletool.cpp tools/ruler/rulertool.cpp
    tools/selection/selectiontool.cpp tools/text/texttool.cpp
    tools/text/textconfig.cpp tools/text/textwidget.cpp
    tools/animannotations.cpp utils/easing.cpp
    utils/colorutils.cpp utils/pathinfo.cpp)
# Animated frames and everything they draw with
set(_anim_frame_sources
    utils/animatedbg.cpp utils/animatedframe.cpp utils/animtimeline.cpp utils/animrender.cpp
    utils/shotmotion.cpp utils/beautifylayout.cpp
    utils/imageeffects.cpp utils/backgroundfx.cpp utils/beautifypalette.cpp
    utils/autocolors.cpp utils/glassframe.cpp)
function(flameshot_add_tool_test name)
  flameshot_add_unit_test(${name} SOURCES ${_drawable_tool_sources} ${ARGN})
  target_sources(${name} PRIVATE fakes/fakeconfighandler.cpp)
  target_compile_definitions(${name} PRIVATE APP_PREFIX="${CMAKE_INSTALL_PREFIX}")
endfunction()
flameshot_add_tool_test(tst_captureproject)
flameshot_add_unit_test(tst_animatedbg SOURCES ${_anim_frame_sources} utils/easing.cpp)
# Draw-itself annotations: timeline, path trimming, each tool's reveal
flameshot_add_tool_test(tst_animreveal utils/animtimeline.cpp)
# Design-director check over every style x motion, with and without annotations
flameshot_add_tool_test(tst_animdirector ${_anim_frame_sources})

if(APPLE)
  # Vision text recognition (Objective-C++, ARC)
  flameshot_add_unit_test(tst_textrecognition
    SOURCES platform/macos/textrecognition.mm
    LIBS "-framework Vision" "-framework CoreGraphics" "-framework Foundation")
  target_compile_options(tst_textrecognition PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)

  # Recording editor export (trim + beautify) against a synthetic MP4 made
  # with AVAssetWriter. The test source is Objective-C++, so it is added by
  # hand instead of through flameshot_add_unit_test.
  add_executable(tst_videoedit tst_videoedit.mm
    "${CMAKE_SOURCE_DIR}/src/platform/macos/videoedit.mm"
    "${CMAKE_SOURCE_DIR}/src/platform/macos/videothumbnail.mm"
    "${CMAKE_SOURCE_DIR}/src/platform/macos/cgimage.mm"
    "${CMAKE_SOURCE_DIR}/src/core/recordingedit.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/beautifylayout.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/imageeffects.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/backgroundfx.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/beautifypalette.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/autocolors.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/glassframe.cpp")
  target_include_directories(tst_videoedit PRIVATE "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(tst_videoedit PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework CoreImage" "-framework CoreGraphics" "-framework Foundation")
  target_compile_options(tst_videoedit PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  add_test(NAME tst_videoedit COMMAND tst_videoedit)
  set_tests_properties(tst_videoedit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Animate export (MP4 + GIF) end to end. Objective-C++, added by hand.
  add_executable(tst_animexport tst_animexport.mm
    "${CMAKE_SOURCE_DIR}/src/platform/macos/animexport.mm"
    "${CMAKE_SOURCE_DIR}/src/platform/macos/videoedit.mm"
    "${CMAKE_SOURCE_DIR}/src/platform/macos/videothumbnail.mm"
    "${CMAKE_SOURCE_DIR}/src/platform/macos/cgimage.mm"
    "${CMAKE_SOURCE_DIR}/src/core/recordingedit.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/animatedbg.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/animatedframe.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/animrender.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/animtimeline.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/shotmotion.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/gifquant.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/beautifylayout.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/imageeffects.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/backgroundfx.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/beautifypalette.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/autocolors.cpp"
    "${CMAKE_SOURCE_DIR}/src/utils/glassframe.cpp")
  # Real annotation tools for the draw-in export (easing comes with them)
  foreach(src IN LISTS _drawable_tool_sources)
    target_sources(tst_animexport PRIVATE "${CMAKE_SOURCE_DIR}/src/${src}")
  endforeach()
  target_sources(tst_animexport PRIVATE fakes/fakeconfighandler.cpp fakes/annotatedsample.cpp)
  target_compile_definitions(tst_animexport PRIVATE APP_PREFIX="${CMAKE_INSTALL_PREFIX}")
  target_include_directories(tst_animexport PRIVATE "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(tst_animexport PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
    "-framework CoreImage" "-framework CoreGraphics" "-framework ImageIO"
    "-framework UniformTypeIdentifiers" "-framework Foundation")
  target_compile_options(tst_animexport PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  add_test(NAME tst_animexport COMMAND tst_animexport)
  set_tests_properties(tst_animexport PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
flameshot_add_unit_test(tst_appidentity SOURCES)

# Tests that need the whole app (real ConfigHandler, widgets, resources) build
# every source of the flameshot target except main.cpp (.ui forms are found
# by AUTOUIC from their includes; listing them in two targets clashes). Each such test
# redirects settings, history and save paths into a temp dir itself.
get_target_property(_fs_sources flameshot SOURCES)
set(_app_sources "")
foreach(src IN LISTS _fs_sources)
  if(NOT IS_ABSOLUTE "${src}")
    set(src "${CMAKE_SOURCE_DIR}/src/${src}")
  endif()
  if(src MATCHES "/main\\.cpp$" OR src MATCHES "\\.(qm|rc|icns|ui)$")
    continue()
  endif()
  list(APPEND _app_sources "${src}")
endforeach()
get_target_property(_fs_defs flameshot COMPILE_DEFINITIONS)
get_target_property(_fs_opts flameshot COMPILE_OPTIONS)
get_target_property(_fs_incs flameshot INCLUDE_DIRECTORIES)
get_target_property(_fs_libs flameshot LINK_LIBRARIES)
list(REMOVE_ITEM _fs_libs project_warnings)

# flameshot_add_app_test(<name> <timeout seconds>)
function(flameshot_add_app_test name timeout)
  add_executable(${name} ${name}.cpp ${_app_sources})
  set_target_properties(${name} PROPERTIES AUTORCC ON AUTOUIC ON)
  target_compile_definitions(${name} PRIVATE ${_fs_defs})
  if(USE_KDSINGLEAPPLICATION)
    # A directory-level definition in src/, so it does not reach this folder
    target_compile_definitions(${name} PRIVATE USE_KDSINGLEAPPLICATION=1)
  endif()
  if(_fs_opts)
    target_compile_options(${name} PRIVATE ${_fs_opts})
  endif()
  target_include_directories(${name} PRIVATE ${_fs_incs} "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(${name} PRIVATE ${_fs_libs} Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT ${timeout})
endfunction()

# Toolbar robot: a real left click on every toolbar button of a live
# CaptureWidget, in the overlay and in the windowed editor. The daemon is
# hosted in-process, so nothing reaches the user's running app.
flameshot_add_app_test(tst_toolbarrobot 300)

# Animate preview dialog (same renderer as the export) and editable Animate
# output: project in the history, Edit on the toast
flameshot_add_app_test(tst_animpreview 180)

# Style packs: parse, serialize and apply against the real ConfigHandler and
# its value checks, with settings in a temp dir
flameshot_add_app_test(tst_stylepack 120)

# Settings window: tall tabs scroll and the window fits a short screen
flameshot_add_app_test(tst_configwindow 120)
