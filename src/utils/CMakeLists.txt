# Required to generate MOC
target_sources(
  flameshot
  PRIVATE abstractlogger.h
          filenamehandler.h
          screengrabber.h
          systemnotification.h
          valuehandler.h
          strfparse.h
)

target_sources(
  flameshot
  PRIVATE abstractlogger.cpp
          filenamehandler.cpp
          screengrabber.cpp
          monitorpreview.cpp
          confighandler.cpp
          systemnotification.cpp
          valuehandler.cpp
          screenshotsaver.cpp
          savepath.cpp
          globalvalues.cpp
          desktopfileparse.cpp
          desktopinfo.cpp
          pathinfo.cpp
          colorutils.cpp
          history.cpp
          capturehistory.cpp
          captureproject.cpp
          stylepack.cpp
          stylepackpreview.cpp
          strfparse.cpp
          imageeffects.cpp
          beautifylayout.cpp
          backgroundfx.cpp
          autocolors.cpp
          glassframe.cpp
          animatedbg.cpp
          animatedframe.cpp
          animrender.cpp
          easing.cpp
          shotmotion.cpp
          animtimeline.cpp
          beautifypalette.cpp
          gifquant.cpp
          scrollstitch.cpp
          loginitem.cpp
          outputpath.cpp
)

IF (UNIX AND NOT APPLE)
target_sources(
  flameshot
  PRIVATE request.h
          request.cpp
)
ENDIF()

IF (WIN32)
  target_sources(
    flameshot
    PRIVATE winlnkfileparse.cpp
  )
ENDIF()
