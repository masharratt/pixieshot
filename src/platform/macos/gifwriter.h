// SPDX-License-Identifier: GPL-3.0-or-later
// Looping GIF file through ImageIO, shared by the recording GIF conversion
// and the animated-background export (and the README demo generator).
// Include from Objective-C++ (ARC) only.

#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>
#include <QtGui/qrgb.h>

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

namespace gifwriter {

// An indexed sRGB colour space with `colors` (at most 256) as its table.
// The caller releases it.
inline CGColorSpaceRef indexedColorSpace(const QVector<QRgb>& colors)
{
    QByteArray table;
    for (QRgb c : colors) {
        table.append(char(qRed(c)));
        table.append(char(qGreen(c)));
        table.append(char(qBlue(c)));
    }
    CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGColorSpaceRef space = CGColorSpaceCreateIndexed(
      rgb,
      size_t(colors.size() - 1),
      reinterpret_cast<const unsigned char*>(table.constData()));
    CGColorSpaceRelease(rgb);
    return space;
}

// An indexed CGImage of a Format_Indexed8 image in `space` (its palette), so
// ImageIO writes these colours instead of picking its own per frame. The
// caller releases it; null on failure.
inline CGImageRef indexedImage(const QImage& indexed, CGColorSpaceRef space)
{
    NSData* bytes = [NSData dataWithBytes:indexed.constBits()
                                   length:size_t(indexed.sizeInBytes())];
    CGDataProviderRef provider =
      CGDataProviderCreateWithCFData((__bridge CFDataRef)bytes);
    CGImageRef image = CGImageCreate(size_t(indexed.width()),
                                     size_t(indexed.height()),
                                     8,
                                     8,
                                     size_t(indexed.bytesPerLine()),
                                     space,
                                     kCGImageAlphaNone,
                                     provider,
                                     NULL,
                                     false,
                                     kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    return image;
}

} // namespace gifwriter
