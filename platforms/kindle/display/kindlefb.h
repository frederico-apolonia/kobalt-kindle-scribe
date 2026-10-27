// Derived from Qt's minimal QPA plugin; LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only.
#pragma once

#include <QtCore/QTimer>
#include <QtCore/QByteArray>
#include <QtGui/QImage>
#include <qpa/qplatformbackingstore.h>
#include <qpa/qplatformintegration.h>
#include <qpa/qplatformscreen.h>
#include <linux/fb.h>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "fbink.h"

QT_BEGIN_NAMESPACE

class QPlatformFontDatabase;
class KindleInput;
class KindleLifecycle;

class KindleFramebuffer final {
public:
    KindleFramebuffer();
    ~KindleFramebuffer();
    KindleFramebuffer(const KindleFramebuffer &) = delete;
    KindleFramebuffer &operator=(const KindleFramebuffer &) = delete;

    QSize size() const { return QSize(int(state.screen_width), int(state.screen_height)); }
    int dpi() const { return int(state.screen_dpi); }
    void present(const QImage &image, const QRegion &region, const QPoint &windowPosition,
                 const QPoint &sourceOffset);
    // Logical screen turned 180 degrees from the panel (inverted portrait): flushes write mirrored.
    void setRotated(bool value) { rotated = value; }
    // Next flush repaints the whole panel with a flashing refresh (first frame, rotation).
    void requestFullRefresh() { fullRefreshPending = true; }
    // While the device sleeps nothing may reach the panel; the first flush after resume is a full refresh.
    void setPaused(bool value) { paused = value; }

private:
    void writeDump() const;
    int descriptor = -1;
    std::uint8_t *memory = nullptr;
    std::size_t memorySize = 0;
    fb_fix_screeninfo fixed{};
    fb_var_screeninfo variable{};
    FBInkConfig partial{};
    FBInkConfig flashing{};
    FBInkState state{};
    bool firstFrame = true;
    bool fullRefreshPending = true;
    bool rotated = false;
    unsigned flushCount = 0;
    bool dryRun = false;
    bool paused = false;
    QByteArray dumpPath;
};

class KindleScreen final : public QPlatformScreen {
public:
    explicit KindleScreen(const KindleFramebuffer &framebuffer);
    QRect geometry() const override { return bounds; }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_ARGB32_Premultiplied; }
    QSizeF physicalSize() const override { return millimeters; }
    QDpi logicalDpi() const override { return {dotsPerInch, dotsPerInch}; }
    QDpi logicalBaseDpi() const override { return {dotsPerInch, dotsPerInch}; }
    QString name() const override { return QStringLiteral("Kindle Scribe"); }
    Qt::ScreenOrientation orientation() const override { return Qt::PortraitOrientation; }
    Qt::ScreenOrientation nativeOrientation() const override { return Qt::PortraitOrientation; }

private:
    QRect bounds;
    QSizeF millimeters;
    qreal dotsPerInch;
};

class KindleIntegration final : public QPlatformIntegration {
public:
    KindleIntegration();
    ~KindleIntegration() override;
    void initialize() override;
    bool hasCapability(Capability capability) const override;
    QPlatformFontDatabase *fontDatabase() const override;
    QPlatformWindow *createPlatformWindow(QWindow *window) const override;
    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override;
    QAbstractEventDispatcher *createEventDispatcher() const override;
    static KindleIntegration *instance();
    KindleFramebuffer &framebuffer() { return output; }
    // The application submitted the frame it wants kept while the device sleeps (it marks its window with
    // "kindlefb_sleep_ready"). If a sleep is pending, output pauses now instead of at the end of the grace period.
    void sleepFrameSubmitted();

private:
    KindleFramebuffer output;
    KindleScreen *primary = nullptr;
    std::unique_ptr<KindleInput> input;
    std::unique_ptr<KindleLifecycle> lifecycle;
    QTimer sleepGrace;
    bool sleepPending = false;
    mutable QPlatformFontDatabase *fonts = nullptr;
};

class KindleBackingStore final : public QPlatformBackingStore {
public:
    explicit KindleBackingStore(QWindow *window) : QPlatformBackingStore(window) {}
    QPaintDevice *paintDevice() override { return &image; }
    void flush(QWindow *window, const QRegion &region, const QPoint &offset) override;
    void resize(const QSize &size, const QRegion &) override;

private:
    QImage image;
};

QT_END_NAMESPACE
