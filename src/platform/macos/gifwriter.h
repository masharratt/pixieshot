// SPDX-License-Identifier: GPL-3.0-or-later
// Looping GIF file through ImageIO, shared by the recording GIF conversion
// and the animated-background export. Include from Objective-C++ (ARC) only.

#pragma once

#include <QString>

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

class GifWriter
{
public:
    // A GIF of exactly `frames` frames that loops forever
    GifWriter(const QString& path, size_t frames)
      : m_url([NSURL fileURLWithPath:[NSString stringWithUTF8String:path.toUtf8()
                                                                      .constData()]])
    {
        m_dest = CGImageDestinationCreateWithURL(
          (__bridge CFURLRef)m_url,
          (__bridge CFStringRef)UTTypeGIF.identifier,
          frames,
          NULL);
        if (m_dest != NULL) {
            CGImageDestinationSetProperties(
              m_dest,
              (__bridge CFDictionaryRef) @{
                  (__bridge NSString*)kCGImagePropertyGIFDictionary : @{
                      (__bridge NSString*)kCGImagePropertyGIFLoopCount : @0
                  }
              });
        }
    }
    ~GifWriter()
    {
        if (m_dest != NULL) {
            CFRelease(m_dest);
        }
    }
    GifWriter(const GifWriter&) = delete;
    GifWriter& operator=(const GifWriter&) = delete;

    bool isOpen() const { return m_dest != NULL; }

    // Shown for `delaySeconds` (GIF stores hundredths)
    void add(CGImageRef image, double delaySeconds)
    {
        NSDictionary* props = @{
            (__bridge NSString*)kCGImagePropertyGIFDictionary : @{
                (__bridge NSString*)kCGImagePropertyGIFDelayTime : @(delaySeconds),
                (__bridge NSString*)kCGImagePropertyGIFUnclampedDelayTime :
                  @(delaySeconds)
            }
        };
        CGImageDestinationAddImage(m_dest, image, (__bridge CFDictionaryRef)props);
    }

    // Writes the file; on failure (or when never opened) nothing is left
    bool finish()
    {
        const bool ok = m_dest != NULL && CGImageDestinationFinalize(m_dest);
        if (!ok) {
            discard();
        }
        return ok;
    }

    void discard() { [[NSFileManager defaultManager] removeItemAtURL:m_url error:nil]; }

private:
    NSURL* m_url;
    CGImageDestinationRef m_dest = NULL;
};
