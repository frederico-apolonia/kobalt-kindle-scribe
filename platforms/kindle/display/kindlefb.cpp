// Derived from Qt's minimal QPA plugin; LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only.
#include "kindlefb.h"
#include "kindleinput.h"
#include "kindlelifecycle.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtGui/private/qfreetypefontdatabase_p.h>
#include <QtGui/private/qgenericunixeventdispatcher_p.h>
#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatformwindow.h>
#include <qpa/qwindowsysteminterface.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>

QT_BEGIN_NAMESPACE

namespace {
[[noreturn]] void fail(const char *operation)
{
    qFatal("kindlefb: %s failed: %s", operation, std::strerror(errno));
}

// Mirrors QOffscreenWindow: a platform window owns its geometry, and the
// fullscreen/maximized states are the plugin's responsibility. Without
// setWindowState() Qt keeps the widget's default size and the first flush
// would not cover the panel.
class KindleWindow final : public QPlatformWindow {
public:
    explicit KindleWindow(QWindow *window) : QPlatformWindow(window), normalGeometry(window->geometry())
    {
        if (window->windowStates() == Qt::WindowNoState)
            setGeometry(window->geometry());
        else
            setWindowState(window->windowStates());
    }

    void setGeometry(const QRect &rect) override
    {
        if (window()->windowStates() != Qt::WindowNoState)
            return;
        applyGeometry(rect);
        normalGeometry = geometry();
    }

    void setWindowState(Qt::WindowStates state) override
    {
        if (state & Qt::WindowMinimized) {
            // nothing to resize
        } else if (state & (Qt::WindowFullScreen | Qt::WindowMaximized)) {
            applyGeometry(screen()->geometry());
        } else {
            applyGeometry(normalGeometry);
        }
        QWindowSystemInterface::handleWindowStateChanged(window(), state);
    }

    void setVisible(bool visible) override
    {
        if (visible == shown)
            return;
        if (visible) {
            if (pendingGeometry) {
                pendingGeometry = false;
                QWindowSystemInterface::handleGeometryChange(window(), geometry());
            }
            QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
            QWindowSystemInterface::handleFocusWindowChanged(window(), Qt::ActiveWindowFocusReason);
        } else {
            QWindowSystemInterface::handleExposeEvent(window(), QRegion());
        }
        shown = visible;
    }

    void requestActivateWindow() override
    {
        if (shown)
            QWindowSystemInterface::handleFocusWindowChanged(window(), Qt::ActiveWindowFocusReason);
    }

private:
    void applyGeometry(const QRect &rect)
    {
        QPlatformWindow::setGeometry(rect);
        if (shown) {
            QWindowSystemInterface::handleGeometryChange(window(), rect);
            QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), rect.size()));
        } else {
            pendingGeometry = true;
        }
        qInfo().nospace() << "kindlefb: window geometry=" << rect.x() << ',' << rect.y() << ' '
                          << rect.width() << 'x' << rect.height();
    }

    QRect normalGeometry;
    bool shown = false;
    bool pendingGeometry = true;
};

double milliseconds(const QElapsedTimer &timer)
{
    return double(timer.nsecsElapsed()) / 1e6;
}
}

KindleFramebuffer::KindleFramebuffer()
{
    partial.is_quiet = true;
    flashing = partial;
    flashing.is_flashing = true;
    descriptor = fbink_open();
    if (descriptor < 0)
        fail("fbink_open");
    if (fbink_init(descriptor, &partial) != 0)
        qFatal("kindlefb: fbink_init failed");
    fbink_get_state(&partial, &state);
    if (ioctl(descriptor, FBIOGET_FSCREENINFO, &fixed) < 0)
        fail("FBIOGET_FSCREENINFO");
    if (ioctl(descriptor, FBIOGET_VSCREENINFO, &variable) < 0)
        fail("FBIOGET_VSCREENINFO");
    if (state.pixel_format != FBINK_PXFMT_Y8 || state.bpp != 8 || variable.bits_per_pixel != 8)
        qFatal("kindlefb: requires Y8/8bpp, got pixel_format=%u state_bpp=%u fb_bpp=%u",
               unsigned(state.pixel_format), unsigned(state.bpp), unsigned(variable.bits_per_pixel));
    if (!state.screen_width || !state.screen_height || fixed.line_length < state.screen_width)
        qFatal("kindlefb: invalid geometry %ux%u stride=%u", state.screen_width, state.screen_height,
               fixed.line_length);
    const std::size_t visibleEnd = std::size_t(variable.yoffset + state.screen_height) * fixed.line_length
                                 + variable.xoffset + state.screen_width;
    if (visibleEnd > fixed.smem_len)
        qFatal("kindlefb: visible buffer exceeds mapping (%zu > %u)", visibleEnd, fixed.smem_len);
    memorySize = fixed.smem_len;
    // KOBALT_KINDLEFB_DRYRUN=<file>: render into private memory, never touch the panel, and write the
    // framebuffer image (same layout as fbguard snapshot) to <file> on exit.
    const char *dryRunPath = std::getenv("KOBALT_KINDLEFB_DRYRUN");
    dryRun = dryRunPath && *dryRunPath;
    void *mapping = dryRun
        ? mmap(nullptr, memorySize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        : mmap(nullptr, memorySize, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    if (mapping == MAP_FAILED)
        fail("mmap");
    memory = static_cast<std::uint8_t *>(mapping);
    if (dryRun) {
        dumpPath = dryRunPath;
        qWarning("kindlefb: DRY RUN - panel untouched, framebuffer image goes to %s", dryRunPath);
    }
    qInfo().nospace() << "kindlefb: ready size=" << state.screen_width << 'x' << state.screen_height
                      << " stride=" << fixed.line_length << " yoffset=" << variable.yoffset
                      << " dpi=" << state.screen_dpi << " rotation=" << unsigned(state.current_rota)
                      << " inverted=" << (state.inverted_grayscale ? 1 : 0)
                      << " fbink=" << fbink_version();
}

KindleFramebuffer::~KindleFramebuffer()
{
    if (!dryRun && flushCount > 0 && fbink_wait_for_complete(descriptor, LAST_MARKER) != 0)
        qWarning("kindlefb: final refresh wait failed");
    if (memory) {
        if (dryRun)
            writeDump();
        munmap(memory, memorySize);
    }
    fbink_close(descriptor);
}

void KindleFramebuffer::writeDump() const
{
    const int file = open(dumpPath.constData(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (file < 0) {
        qWarning("kindlefb: cannot open dump %s: %s", dumpPath.constData(), std::strerror(errno));
        return;
    }
    std::size_t offset = 0;
    while (offset < memorySize) {
        const ssize_t count = write(file, memory + offset, memorySize - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            qWarning("kindlefb: dump stopped at %zu/%zu: %s", offset, memorySize, std::strerror(errno));
            break;
        }
        offset += std::size_t(count);
    }
    fsync(file);
    close(file);
    qInfo("kindlefb: dump wrote %zu/%zu bytes to %s", offset, memorySize, dumpPath.constData());
}

void KindleFramebuffer::present(const QImage &source, const QRegion &damage, const QPoint &windowPosition,
                                const QPoint &sourceOffset)
{
    if (paused)
        return;
    const QRect screenRect(QPoint(), size());
    // Screen-space rectangle backed by valid pixels: nothing outside it is ever read from `source`.
    const QRect covered = QRect(windowPosition - sourceOffset, source.size()).intersected(screenRect);
    if (covered.isEmpty())
        return;
    if (firstFrame && covered != screenRect)
        qWarning().nospace() << "kindlefb: window covers " << covered.width() << 'x' << covered.height()
                             << " of " << screenRect.width() << 'x' << screenRect.height()
                             << "; the rest of the panel is cleared to white";
    const QRegion destinationDamage = damage.translated(windowPosition).intersected(covered);
    if (destinationDamage.isEmpty() && !fullRefreshPending)
        return;
    // `destination` is in logical screen coordinates (what Qt draws); `native` is the same pixels on the panel.
    const QRect destination = fullRefreshPending ? covered : destinationDamage.boundingRect();
    const QRect native = rotated ? QRect(screenRect.width() - destination.right() - 1,
                                         screenRect.height() - destination.bottom() - 1,
                                         destination.width(), destination.height())
                                 : destination;
    const QRect refresh = fullRefreshPending ? screenRect : native;
    const QPoint sourceOrigin = destination.topLeft() - windowPosition + sourceOffset;
    const QRect sourceRect(sourceOrigin, destination.size());
    if (!source.rect().contains(sourceRect)) {
        qWarning("kindlefb: dropping flush, source rectangle outside backing store");
        return;
    }

    QElapsedTimer timer;
    timer.start();
    const std::uint8_t white = state.inverted_grayscale ? 0 : 255;
    if (fullRefreshPending) {
        for (int row = 0; row < screenRect.height(); ++row)
            std::memset(memory + std::size_t(variable.yoffset + row) * fixed.line_length + variable.xoffset,
                        white, std::size_t(screenRect.width()));
    }
    const auto toPanel = [this](QRgb pixel) {
        const int gray = std::min(qGray(pixel) + 255 - qAlpha(pixel), 255);
        return state.inverted_grayscale ? std::uint8_t(255 - gray) : std::uint8_t(gray);
    };
    const int width = destination.width();
    for (int row = 0; row < destination.height(); ++row) {
        const QRgb *input = reinterpret_cast<const QRgb *>(source.constScanLine(sourceRect.top() + row))
                          + sourceRect.left();
        // A 180 degree turn mirrors rows and columns of the destination rectangle.
        const int panelRow = rotated ? native.top() + destination.height() - 1 - row : native.top() + row;
        std::uint8_t *output = memory + std::size_t(variable.yoffset + panelRow) * fixed.line_length
                             + variable.xoffset + native.left();
        if (rotated) {
            for (int column = 0; column < width; ++column)
                output[width - 1 - column] = toPanel(input[column]);
        } else {
            for (int column = 0; column < width; ++column)
                output[column] = toPanel(input[column]);
        }
    }
    __sync_synchronize();
    const double convertMs = milliseconds(timer);
    double submitMs = 0;
    double waitMs = 0;
    if (!dryRun) {
        timer.restart();
        FBInkConfig *configuration = fullRefreshPending ? &flashing : &partial;
        if (fbink_refresh(descriptor, unsigned(refresh.top()), unsigned(refresh.left()),
                          unsigned(refresh.width()), unsigned(refresh.height()), configuration) != 0)
            qFatal("kindlefb: fbink_refresh failed");
        submitMs = milliseconds(timer);
        if (fullRefreshPending) {
            timer.restart();
            if (fbink_wait_for_complete(descriptor, LAST_MARKER) != 0)
                qWarning("kindlefb: full-refresh wait failed");
            waitMs = milliseconds(timer);
        }
    }
    ++flushCount;
    qInfo().nospace() << "kindlefb: flush=" << flushCount << " rects=" << destinationDamage.rectCount()
                      << " blit=" << destination.x() << ',' << destination.y() << ' '
                      << destination.width() << 'x' << destination.height()
                      << " refresh=" << refresh.x() << ',' << refresh.y() << ' '
                      << refresh.width() << 'x' << refresh.height()
                      << " rot=" << (rotated ? 180 : 0)
                      << " mode=" << (dryRun ? "dry-" : "") << (fullRefreshPending ? "full" : "partial")
                      << " convert_ms=" << convertMs << " submit_ms=" << submitMs
                      << " wait_ms=" << waitMs;
    firstFrame = false;
    fullRefreshPending = false;
}

KindleScreen::KindleScreen(const KindleFramebuffer &framebuffer)
    : bounds(QPoint(), framebuffer.size())
    , dotsPerInch(framebuffer.dpi() > 0 ? framebuffer.dpi() : 300)
{
    millimeters = QSizeF(bounds.width() * 25.4 / dotsPerInch, bounds.height() * 25.4 / dotsPerInch);
}

KindleIntegration::KindleIntegration()
{
    primary = new KindleScreen(output);
    QWindowSystemInterface::handleScreenAdded(primary);
}

KindleIntegration::~KindleIntegration()
{
    input.reset();
    QWindowSystemInterface::handleScreenRemoved(primary);
    delete fonts;
}

// Qt's screen orientation is the one source of truth for pixel and input rotation: whoever turns the QScreen
// (the application's reader-orientation policy, or the accelerometer when KOBALT_KINDLE_AUTOROTATE=1) makes the
// plugin mirror the panel output and the pen/touch coordinates together, then repaint with a flashing refresh.
void KindleIntegration::initialize()
{
    QScreen *screen = primary->screen();
    QObject::connect(screen, &QScreen::orientationChanged, screen, [this](Qt::ScreenOrientation orientation) {
        const bool inverted = orientation == Qt::InvertedPortraitOrientation;
        if (!inverted && orientation != Qt::PortraitOrientation)
            qWarning("kindlefb: orientation %d is not supported; staying upright", int(orientation));
        output.setRotated(inverted);
        if (input) {
            input->releaseAll();
            input->setRotated(inverted);
        }
        output.requestFullRefresh();
        for (QWindow *window : QGuiApplication::allWindows()) {
            if (window->isVisible())
                QWindowSystemInterface::handleExposeEvent(window, QRect(QPoint(), window->size()));
        }
        qInfo("kindlefb: orientation %s, panel rotation %d", inverted ? "inverted portrait" : "portrait",
              inverted ? 180 : 0);
    });
    const bool autoRotate = qEnvironmentVariableIntValue("KOBALT_KINDLE_AUTOROTATE") != 0;
    input = std::make_unique<KindleInput>(output.size(), [screen, autoRotate](Qt::ScreenOrientation orientation) {
        if (autoRotate)
            QWindowSystemInterface::handleScreenOrientationChange(screen, orientation);
    });
    const Qt::ScreenOrientation initial = KindleInput::initialOrientation();
    if (autoRotate && initial == Qt::InvertedPortraitOrientation) {
        output.setRotated(true);
        input->setRotated(true);
        QWindowSystemInterface::handleScreenOrientationChange(screen, initial);
    }

    // Sleep and wake belong to powerd; the plugin only quiesces input before and repaints after. Skipped in a
    // dry run or replay so a test never listens to the real device's power events.
    if ((qEnvironmentVariableIsEmpty("KOBALT_KINDLEFB_DRYRUN") && qEnvironmentVariableIsEmpty("KOBALT_KINDLE_REPLAY"))
        || qEnvironmentVariableIntValue("KOBALT_KINDLE_LIFECYCLE") != 0) {
        lifecycle = std::make_unique<KindleLifecycle>();
        // An application that paints its own sleep picture (it sets the "kindlefb_sleep_screen" property on the
        // QGuiApplication) is given until it submits that frame, and at most four seconds, before output pauses.
        sleepGrace.setSingleShot(true);
        QObject::connect(&sleepGrace, &QTimer::timeout, lifecycle.get(), [this] {
            sleepPending = false;
            output.setPaused(true);
        });
        QObject::connect(lifecycle.get(), &KindleLifecycle::goingToSleep, lifecycle.get(), [this](int) {
            input->releaseAll();
            if (QGuiApplication::instance() && QGuiApplication::instance()->property("kindlefb_sleep_screen").toBool()) {
                sleepPending = true;
                sleepGrace.start(4000);
            } else {
                output.setPaused(true);
            }
        });
        QObject::connect(lifecycle.get(), &KindleLifecycle::wokeUp, lifecycle.get(), [this](int) {
            sleepPending = false;
            sleepGrace.stop();
            input->releaseAll();
            output.setPaused(false);
            output.requestFullRefresh();
            for (QWindow *window : QGuiApplication::allWindows()) {
                if (window->isVisible())
                    QWindowSystemInterface::handleExposeEvent(window, QRect(QPoint(), window->size()));
            }
            qInfo("kindlefb: woke up, full repaint requested");
        });
    }
}

bool KindleIntegration::hasCapability(Capability capability) const
{
    switch (capability) {
    case ThreadedPixmaps: return true;
    case MultipleWindows: return false;
    case RhiBasedRendering: return false;
    default: return QPlatformIntegration::hasCapability(capability);
    }
}

QPlatformFontDatabase *KindleIntegration::fontDatabase() const
{
    if (!fonts)
        fonts = new QFreeTypeFontDatabase;
    return fonts;
}

QPlatformWindow *KindleIntegration::createPlatformWindow(QWindow *window) const
{
    return new KindleWindow(window);
}

QPlatformBackingStore *KindleIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new KindleBackingStore(window);
}

QAbstractEventDispatcher *KindleIntegration::createEventDispatcher() const
{
    return createUnixEventDispatcher();
}

KindleIntegration *KindleIntegration::instance()
{
    return static_cast<KindleIntegration *>(QGuiApplicationPrivate::platformIntegration());
}

void KindleBackingStore::flush(QWindow *window, const QRegion &region, const QPoint &offset)
{
    auto &framebuffer = KindleIntegration::instance()->framebuffer();
    // The application marks the window when it wants a clean redraw (Kobalt's pageRefresh(clean)).
    if (window->property("kindlefb_full_refresh").toBool()) {
        window->setProperty("kindlefb_full_refresh", false);
        framebuffer.requestFullRefresh();
    }
    framebuffer.present(image, region, window->geometry().topLeft(), offset);
    if (window->property("kindlefb_sleep_ready").toBool()) {
        window->setProperty("kindlefb_sleep_ready", false);
        KindleIntegration::instance()->sleepFrameSubmitted();
    }
}

void KindleIntegration::sleepFrameSubmitted()
{
    if (!sleepPending)
        return;
    sleepPending = false;
    sleepGrace.stop();
    output.setPaused(true);
}

void KindleBackingStore::resize(const QSize &size, const QRegion &)
{
    if (image.size() != size) {
        image = QImage(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        qInfo().nospace() << "kindlefb: backing store " << size.width() << 'x' << size.height();
    }
}

QT_END_NAMESPACE
