# README demo GIF generator: renders demoreel scenes with the app's own
# Animate renderer and writes them through the export's GIF path. Built like
# the whole-app tests: every flameshot source except main.cpp.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test Svg Network DBus)
set(CMAKE_AUTOMOC ON)

get_target_property(_demo_fs_sources flameshot SOURCES)
set(_demo_sources "")
foreach(src IN LISTS _demo_fs_sources)
  if(NOT IS_ABSOLUTE "${src}")
    set(src "${CMAKE_SOURCE_DIR}/src/${src}")
  endif()
  if(src MATCHES "/main\\.cpp$" OR src MATCHES "\\.(qm|rc|icns|ui)$")
    continue()
  endif()
  list(APPEND _demo_sources "${src}")
endforeach()
get_target_property(_demo_defs flameshot COMPILE_DEFINITIONS)
get_target_property(_demo_opts flameshot COMPILE_OPTIONS)
get_target_property(_demo_incs flameshot INCLUDE_DIRECTORIES)
get_target_property(_demo_libs flameshot LINK_LIBRARIES)
list(REMOVE_ITEM _demo_libs project_warnings)

add_executable(pixie_demogif main.mm demoreel.cpp demoshot.cpp ${_demo_sources})
set_target_properties(pixie_demogif PROPERTIES AUTORCC ON AUTOUIC ON)
target_compile_definitions(pixie_demogif PRIVATE ${_demo_defs})
if(USE_KDSINGLEAPPLICATION)
  target_compile_definitions(pixie_demogif PRIVATE USE_KDSINGLEAPPLICATION=1)
endif()
if(_demo_opts)
  target_compile_options(pixie_demogif PRIVATE ${_demo_opts})
endif()
target_compile_options(pixie_demogif PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
target_include_directories(pixie_demogif PRIVATE ${_demo_incs} "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(pixie_demogif PRIVATE ${_demo_libs} Qt6::Test
  "-framework ImageIO" "-framework UniformTypeIdentifiers"
  "-framework CoreGraphics" "-framework Foundation")
