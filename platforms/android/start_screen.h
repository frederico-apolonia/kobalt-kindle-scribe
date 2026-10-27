#pragma once
#include "interfaces/reader_interface.h"
#include <QStackedWidget>

namespace kobalt::library {
class LibraryView;
class Store;
}

namespace kobalt::android {
class WidgetCanvas;
// Qt-only library and book-opening UI. The host supplies document and folder
// picking, file access and exit.
class StartScreen final : public QStackedWidget {
    Q_OBJECT
public:
    StartScreen(PresentationFactory presentation, library::Store &store, QWidget *parent = nullptr);
    ~StartScreen() override;
    void showOpening();
    void setProgress(int percent);
    // Closes the library's topmost layer. False at the root library.
    bool dismissModal();
    void showLibrary();
    bool libraryShown() const;
    // Follows the reader's dark mode setting.
    void setDark(bool dark);
    library::LibraryView *library();
    WidgetCanvas *libraryCanvas();
    WidgetCanvas *openingCanvas() const { return openingSurface; }
    // Separate the library from Android's system bars; the reader stays fullscreen.
    void setLibrarySystemBarBorders(bool visible);
    // By default the library fills its canvas at the display's pixel ratio,
    // as on Android. A host that scales the reader itself, like the desktop
    // app, gives the library the same reader size and scale.
    void setLibraryScale(QSize pixels, qreal scale, bool nativePixels, qreal rasterScale);
    // An opening error for a book chosen in the library, shown over the library.
    void showLibraryError(const QString &error);
    // On Android a book opened from the library on its own is Kobalt's copy,
    // which removal deletes. The confirmation says so.
    void setSingleBooksAreCopies(bool value);
    // A host without document pickers hides the add-folder and open-book choices; one without a system back action
    // adds an exit choice that emits exitRequested.
    void setHostActions(bool pickers, bool exit);
signals:
    void exitRequested();
    // From the library: open one of its books, add a folder, or open and add
    // a single book.
    void bookRequested(QString key);
    void addFolderRequested();
    void libraryOpenRequested();
    void removeBookRequested(QString key);
    void markUnreadRequested(QString key);
    void removeFolderRequested(QString key);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    std::unique_ptr<ReaderOpeningView> opening;
    PresentationFactory presentation;
    library::Store &store;
    WidgetCanvas *canvas = nullptr, *openingSurface = nullptr;
    library::LibraryView *view = nullptr;
    bool dark = false, hostScaled = false, singleBooksAreCopies = false;
    struct Scale {
        QSize pixels;
        qreal scale = 1, rasterScale = 1;
        bool nativePixels = false;
    } scale;
    void buildLibrary();
    void fitLibrary();
};
}
