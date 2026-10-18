target_sources(flameshot PRIVATE
    actionlist.h
    actionrunner.h
    capturedelay.h
    capturefirstrequest.h
    flameshot.h
    hotkeyutils.h
    flameshotdaemon.h
    qguiappcurrentscreen.h
    gifplan.h
    recordingcontroller.h
    recordinggeometry.h
    recordingoptions.h
    recordingstate.h
    recordingedit.h
    scrollcapturecontroller.h
    scrollcapturestate.h
)

target_sources(flameshot PRIVATE
    actionlist.cpp
    actionrunner.cpp
    capturedelay.cpp
    capturefirstrequest.cpp
    capturerequest.cpp
    gifplan.cpp
    recordingcontroller.cpp
    recordinggeometry.cpp
    recordingoptions.cpp
    recordingstate.cpp
    recordingedit.cpp
    scrollcapturecontroller.cpp
    scrollcapturestate.cpp
    flameshot.cpp
    flameshotdaemon.cpp
    hotkeyutils.cpp
    qguiappcurrentscreen.cpp
)

if (APPLE)
    # Animate export (the encoder lives in platform/macos)
    target_sources(flameshot PRIVATE animexportjob.h animexportjob.cpp)
endif ()

if (UNIX AND NOT APPLE)
target_sources(flameshot PRIVATE
    flameshotdbusadapter.h
    flameshotdbusadapter.cpp
)
endif ()

if (USE_KDSINGLEAPPLICATION)
if (NOT WIN32)
target_sources(flameshot PRIVATE
    signaldaemon.h
    signaldaemon.cpp
)
endif ()
endif ()

if (WIN32)
    target_sources(flameshot PRIVATE globalshortcutfilter.h globalshortcutfilter.cpp)
endif ()
