target_sources(flameshot PRIVATE
    windowlist.h
    windowpick.h
    windowpick.cpp
    screenrecorder.h
    scrollcapture.h
    gifencoder.h
    videothumbnail.h
    videoedit.h
    videopreview.h
    windowspace.h
    screenaccess.h
)

if (APPLE)
    target_sources(flameshot PRIVATE
        macos/windowlist_mac.cpp
        macos/screenrecorder.mm
        macos/scrollcapture_mac.mm
        macos/sckdisplay.h
        macos/sckdisplay.mm
        macos/cgimage.h
        macos/cgimage.mm
        macos/gifencoder.mm
        macos/videothumbnail.mm
        macos/videoedit.mm
        macos/videopreview.mm
        macos/windowspace_mac.mm
        macos/screenaccess_mac.mm
        macos/textrecognition.h
        macos/textrecognition.mm
    )
    # ARC for every Objective-C++ file. A set_source_files_properties() call
    # here would not reach the flameshot target, which is defined in another
    # directory, so the flag goes on the target by language instead.
    target_compile_options(flameshot PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    # ScreenCaptureKit is linked weakly so the app still starts on macOS
    # versions that lack it; recording is hidden there.
    target_link_libraries(flameshot
        "-framework CoreGraphics"
        "-framework CoreFoundation"
        "-framework AppKit"
        "-framework ApplicationServices"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreImage"
        "-framework QuartzCore"
        "-framework ImageIO"
        "-framework UniformTypeIdentifiers"
        "-framework Vision"
        "-weak_framework ScreenCaptureKit"
    )
else ()
    target_sources(flameshot PRIVATE windowlist_stub.cpp recording_stub.cpp
        windowspace_stub.cpp scrollcapture_stub.cpp screenaccess_stub.cpp)
endif ()
