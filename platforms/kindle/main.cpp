#include "../android/canvas.h"
#include "../android/reader.h"
#include "../android/start_screen.h"
#include "keyboard.h"
#include "lifecycle.h"
#include "light_device.h"
#include "services.h"
#include "assets/assets.h"
#include "assets/bundled_fonts.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "library/library_store.h"
#include "library/scanner.h"
#include "reading/reading_density.h"
#include "storage/reader_storage.h"
#include "support/diagnostics.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QEvent>
#include <QLineEdit>
#include <QStackedLayout>
#include <QTimer>
#include <execinfo.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unistd.h>
#include <utility>

using namespace kobalt;
namespace {
// Everything Kobalt owns lives under one folder on the user volume. Books are only read, from /mnt/us/documents.
QString homeFolder() {
    return qEnvironmentVariable("KOBALT_HOME", QStringLiteral("/mnt/us/kobalt"));
}
QString libraryFolder() {
    return qEnvironmentVariable("KOBALT_LIBRARY", QStringLiteral("/mnt/us/documents"));
}

// Test hook (KOBALT_KINDLE_TRACE=1): log every touch and mouse event the application receives.
class Trace final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *object, QEvent *event) override {
        switch (event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::TabletPress:
        case QEvent::TabletRelease:
            if (++count <= 3000)
                qInfo() << "trace:" << event->type() << object->metaObject()->className()
                        << (object->objectName().isEmpty() ? "" : object->objectName());
            break;
        default:
            break;
        }
        return false;
    }

private:
    int count = 0;
};

class Window : public QWidget {
public:
    Window(EngineFontInputs fonts, QString placeholder, std::shared_ptr<kindle::LightDevice> light)
        : controller(storage::Paths{homeFolder() + "/state/config"}, std::move(fonts), std::move(placeholder),
                     [this, light] { return kindle::readerServices(light, keyboard); }, [](Qt::ScreenOrientation) {}),
          store(homeFolder() + "/state/config", std::make_shared<library::LocalSource>()) {
        setWindowTitle("Kobalt");
        QPalette colors;
        colors.setColor(QPalette::Window, Qt::white);
        colors.setColor(QPalette::WindowText, Qt::black);
        colors.setColor(QPalette::Base, Qt::white);
        colors.setColor(QPalette::Text, Qt::black);
        setPalette(colors);
        setAutoFillBackground(true);
        layout = new QStackedLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        start = new android::StartScreen(kindle::readerServices(light, nullptr).presentation, store);
        start->setDark(controller.preferences().value("dark_mode").toBool(false));
        // No document pickers on the Kindle, and no back key: the library's More sheet offers Exit.
        start->setHostActions(false, true);
        canvas = new android::WidgetCanvas(this);
        layout->addWidget(start);
        layout->addWidget(canvas);
        canvas->viewportResized = [this] { canvas->fitContent(); };
        if (qEnvironmentVariableIntValue("KOBALT_KINDLE_TRACE")) {
            const auto battery = kindle::readerServices(light, nullptr).battery();
            qInfo() << "kindle battery" << battery.percent << "charging" << battery.charging << "full" << battery.fullyCharged;
        }
        // There is no system keyboard: dock Kobalt's own while a text field, in the library or a reader, has focus.
        // The fields live inside graphics scenes, where the application's focus widget is the view, not the field,
        // so the field's own focus events are the signal.
        keyboard = new kindle::OnScreenKeyboard(this);
        qApp->installEventFilter(this);
        connect(start, &android::StartScreen::bookRequested, this, &Window::openBook);
        connect(start, &android::StartScreen::exitRequested, this, &QWidget::close);
        connect(start, &android::StartScreen::markUnreadRequested, this, [this](const QString &key) {
            if (controller.resetBook(key))
                store.setMark(key, library::Book::Mark::Unread);
            else
                start->showLibraryError(tr("The book's reading settings and position could not be reset."));
        });
        // Books stay where they are: removing one only forgets it, and folders are fixed to the documents folder.
        connect(start, &android::StartScreen::removeBookRequested, this, [this](const QString &key) {
            store.removeBook(key);
        });
        connect(&controller, &android::ReaderController::openingProgress, start, &android::StartScreen::setProgress);
        connect(&controller, &android::ReaderController::opened, this, [this] {
            auto *reader = controller.window();
            canvas->setContent(reader, true, 1.0);
            setPalette(reader->palette());
            layout->setCurrentWidget(canvas);
            canvas->fitContent();
            reader->show();
            setUpdatesEnabled(true);
            reader->checkpoint();
            qInfo() << "Kobalt frame ready";
        });
        connect(&controller, &android::ReaderController::failed, this, [this](const QString &error) {
            failed = true;
            setPalette(start->palette());
            start->showLibraryError(error);
            layout->setCurrentWidget(start);
            qWarning() << "Kobalt reader error:" << error;
        });
        connect(&controller, &android::ReaderController::closed, this, [this] {
            recordProgress();
            setPalette(start->palette());
            start->setDark(controller.preferences().value("dark_mode").toBool(false));
            if (!failed)
                start->showLibrary();
            layout->setCurrentWidget(start);
            if (closing) {
                QCoreApplication::quit();
            }
        });
        connect(&lifecycle, &kindle::Lifecycle::goingToSleep, this, [this](int) {
            if (auto *reader = controller.window())
                reader->checkpoint();
        });
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive && controller.window())
                controller.window()->checkpoint();
        });
        library::Folder documents{libraryFolder(), tr("Documents")};
        bool known = false;
        for (const auto &folder : store.index().folders)
            known = known || folder.key == documents.key;
        if (!known && QFileInfo(documents.key).isDir())
            store.addFolder(documents);
        QTimer::singleShot(0, this, [this] {
            if (store.index().settings.checkAtStart)
                store.scan();
        });
        // Test hook: open a book without touch input, after the window has settled.
        // Test hook: KOBALT_KINDLE_TYPE taps text out on the keyboard, in the library search or, with
        // KOBALT_KINDLE_OPEN, in the book's search. "<shift>", "<back>", "<space>", "<enter>", "?123" and "abc"
        // in angle brackets name the other keys.
        const auto typed = qEnvironmentVariable("KOBALT_KINDLE_TYPE");
        if (!typed.isEmpty()) {
            const bool inBook = !qEnvironmentVariableIsEmpty("KOBALT_KINDLE_OPEN");
            const char *opener = inBook ? "rrSearchOpen" : "librarySearch";
            // The library and the reader are drawn inside canvases, so they are not among the window's children.
            QTimer::singleShot(inBook ? 12000 : 3000, this, [opener] {
                for (auto *widget : QApplication::allWidgets())
                    if (widget->objectName() == opener)
                        static_cast<QAbstractButton *>(widget)->click();
            });
            QTimer::singleShot(inBook ? 14000 : 5000, this, [this, typed] {
                for (qsizetype i = 0; i < typed.size(); ++i) {
                    QString label(typed[i]);
                    if (typed[i] == '<' && typed.indexOf('>', i) > i) {
                        label = typed.mid(i, typed.indexOf('>', i) - i + 1);
                        i += label.size() - 1;
                    }
                    if (label == "<dismiss>") {
                        keyboard->dismiss();
                    } else if (label == "<submit>") {
                        for (auto *widget : QApplication::allWidgets())
                            if (widget->objectName() == "rrSearchSubmit")
                                static_cast<QAbstractButton *>(widget)->click();
                    } else if (!keyboard->tap(label)) {
                        qWarning() << "KOBALT_KINDLE_TYPE: no key" << label;
                    }
                }
            });
        }
        const auto autoOpen = qEnvironmentVariable("KOBALT_KINDLE_OPEN");
        if (!autoOpen.isEmpty())
            QTimer::singleShot(3000, this, [this, autoOpen] { openBook(autoOpen); });
    }

private:
    // Declared first: the reader's services are built from it, and a reader may be opened at any time.
    kindle::OnScreenKeyboard *keyboard = nullptr;
    android::ReaderController controller;
    library::Store store;
    kindle::Lifecycle lifecycle;
    QStackedLayout *layout;
    android::StartScreen *start;
    android::WidgetCanvas *canvas;
    QString openedKey;
    bool closing = false, failed = false, layoutRepairPending = false;
    QSize pixels() const {
        if (canvas->isVisible())
            return canvas->pixelSize();
        const auto area = contentsRect().size();
        return {qRound(area.width() * devicePixelRatioF()), qRound(area.height() * devicePixelRatioF())};
    }
    void recordProgress() {
        if (openedKey.isEmpty())
            return;
        const auto progress = library::savedProgress(controller.savedState(openedKey));
        store.setProgress(std::exchange(openedKey, {}), progress.percent, progress.finished);
    }
    void openBook(const QString &path) {
        if (closing)
            return;
        recordProgress();
        const auto key = QFileInfo(path).absoluteFilePath();
        if (!store.index().books.contains(key))
            store.addBook(path);
        openedKey = store.index().books.contains(key) ? key : QString();
        if (!openedKey.isEmpty())
            store.setOpened(openedKey);
        failed = false;
        start->showOpening();
        layout->setCurrentWidget(start);
        controller.open(path, pixels());
    }
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::FocusIn) {
            auto *edit = qobject_cast<QLineEdit *>(watched);
            if (edit && !edit->isReadOnly()) {
                keyboard->submitted = {};
                keyboard->attach(edit);
            }
        } else if (event->type() == QEvent::FocusOut && watched == keyboard->target()) {
            keyboard->dismiss();
        }
        auto *reader = controller.window();
        if (reader && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            auto *widget = qobject_cast<QWidget *>(watched);
            if (widget && (widget == reader || reader->isAncestorOf(widget)) && !layoutRepairPending) {
                layoutRepairPending = true;
                QTimer::singleShot(0, this, [this] {
                    layoutRepairPending = false;
                    auto *reader = controller.window();
                    if (!reader)
                        return;
                    // Proxy resize can leave newly created children with an inherited disabled-update flag, which
                    // keeps them from ever painting (search results showed only their last row). Explicit disables stay.
                    for (auto *child : reader->findChildren<QWidget *>())
                        if (!child->updatesEnabled() && !child->testAttribute(Qt::WA_ForceUpdatesDisabled) &&
                            child->parentWidget() && child->parentWidget()->updatesEnabled())
                            child->setUpdatesEnabled(true);
                });
            }
        }
        return QWidget::eventFilter(watched, event);
    }
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        if (keyboard && keyboard->isVisible())
            keyboard->place();
    }
    void closeEvent(QCloseEvent *event) override {
        if (closing && controller.idle()) {
            event->accept();
            return;
        }
        event->ignore();
        if (closing)
            return;
        if (start->dismissModal())
            return;
        if (auto *reader = controller.window()) {
            reader->close();
            return;
        }
        if (!controller.idle()) {
            controller.close();
            return;
        }
        closing = true;
        controller.close();
        QCoreApplication::quit();
    }
};
}
// A crash on the device leaves nothing but an exit code. Print the faulting address and the raw stack, with the
// module map so the frames can be resolved against the unstripped build. Runs on its own stack so an overflow reports too.
static void crashHandler(int signal, siginfo_t *info, void *) {
    char head[96];
    const int length = snprintf(head, sizeof head, "kobalt: fatal signal %d at %p\n", signal, info->si_addr);
    if (length > 0)
        (void)!write(2, head, size_t(length));
    void *frames[48];
    backtrace_symbols_fd(frames, backtrace(frames, 48), 2);
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[256];
    while (maps && fgets(line, sizeof line, maps))
        if (strstr(line, "kobalt") || strstr(line, "lib"))
            (void)!write(2, line, strlen(line));
    _exit(128 + signal);
}
static void installCrashHandler() {
    static char stack[64 * 1024];
    stack_t alternate{stack, 0, sizeof stack};
    sigaltstack(&alternate, nullptr);
    struct sigaction action {};
    action.sa_sigaction = crashHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
        sigaction(signal, &action, nullptr);
}

int main(int argc, char **argv) {
    FontCopies copies;
    installCrashHandler();
    QApplication app(argc, argv);
    app.setApplicationName("Kobalt");
    app.setOrganizationName("Kobalt");
    app.setQuitOnLastWindowClosed(false);
    AssetBundle assets(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("assets"),
                       QStringLiteral(KOBALT_VERSION));
    if (!assets.available() || !ui::initializeInterfaceFonts(assets)) {
        qCritical() << "Kobalt bundled assets are unavailable";
        return 2;
    }
    for (const auto &file : bundledFontFiles(assets))
        if (QFontDatabase::addApplicationFont(file) < 0) {
            qCritical() << "Cannot register font" << file;
            return 2;
        }
    // The panel is 300 ppi. KOBALT_KINDLE_DPI scales the interface and book text together; larger is bigger.
    const int density = qBound(150, qEnvironmentVariableIntValue("KOBALT_KINDLE_DPI") ? qEnvironmentVariableIntValue("KOBALT_KINDLE_DPI") : 390, 600);
    readingPpi.store(density);
    // Books size images in CSS pixels, at 96 per inch: a 736 pixel figure is 7.7 inches. On this 10.2 inch panel at
    // the interface density that is the whole page, so CSS lengths use a lower density (KOReader's 160 by default).
    cssPpi.store(qBound(48, qEnvironmentVariableIntValue("KOBALT_KINDLE_CSS_DPI") ? qEnvironmentVariableIntValue("KOBALT_KINDLE_CSS_DPI") : 160, 600));
    ui::setDensity(density);
    if (qEnvironmentVariableIntValue("KOBALT_KINDLE_TRACE"))
        app.installEventFilter(new Trace(&app));
    QApplication::setFont(ui::interfaceFont());
    initializeDiagnostics(homeFolder() + "/state/diagnostics");
    auto light = std::make_shared<kindle::LightDevice>();
    Window window(engineFontInputs(assets, {}, homeFolder() + "/fonts", {}), placeholderFontFamily(assets, copies), light);
    window.showFullScreen();
    // Test hook: leave cleanly after a number of seconds so a dry run can dump its last frame.
    const int exitAfter = qEnvironmentVariableIntValue("KOBALT_KINDLE_EXIT_AFTER");
    if (exitAfter > 0)
        QTimer::singleShot(exitAfter * 1000, &app, [] { QCoreApplication::exit(0); });
    qInfo() << "Kobalt Kindle ready";
    return app.exec();
}
