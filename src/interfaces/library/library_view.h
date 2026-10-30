#pragma once
#include "interfaces/reader/ui/controls/panels.h"
#include "library/library_index.h"
#include "platform/api/keyboard_controller.h"
#include <functional>
#include <memory>

class QTimer;
class QLineEdit;
class QPushButton;
namespace kobalt::ui {
class CellButton;
class PagerBar;
class ModalOverlay;
class ActionMenu;
class LoadingIndicator;
}
namespace kobalt {
class LightControls;
}
namespace kobalt::library {
class BookPages;
class FolderPage;
class SettingsPage;
class SortSheet;
class MoreSheet;

// The Android and desktop library. It draws in device pixels on the reader's
// canvas, with the reader's header, rows, pager and dialogs. The host owns the
// index and every file operation; the view only reports what was chosen.
class LibraryView final : public ui::Panel {
public:
    LibraryView(const Index &index, std::function<QString(const QString &)> coverPath, QWidget *parent = nullptr);
    ~LibraryView() override;
    // Call after the index changes. While scanning, the view redraws at most
    // once a second, which keeps e-paper refreshes down.
    void refresh();
    void setScanning(bool scanning, int found);
    void setDark(bool dark);
    void setKeyboardFactory(KeyboardFactory factory);
    void showNotice(const QString &title, const QString &message);
    // The system back action: closes the topmost layer. False when only the
    // library itself is left.
    bool dismiss();
    void pageButton(int direction);
    // Looks the status up again and redraws the header's status if it changed. Call it when the library is shown
    // after the device slept; while visible, the view checks every few seconds by itself.
    void refreshStatus();

    // The clock and battery drawn at the right of the header. Hosts that report neither never call setStatus.
    struct HeaderStatus {
        QString time;
        // Percent; negative when unknown, which leaves out the battery.
        int battery = -1;
        bool charging = false;
        bool operator==(const HeaderStatus &other) const {
            return time == other.time && battery == other.battery && charging == other.charging;
        }
    };
    // `source` is asked for the status when the library is shown and every few seconds while it stays visible.
    void setStatus(std::function<HeaderStatus()> source);

    // A row of the Settings page that the host owns. With two or more `choices` it picks one of them, `value`
    // returns the index of the current one, and `details` (optional, parallel to `choices`) explain each. With none
    // it is a switch, and `value` is 0 or 1.
    struct HostOption {
        QString name, title, description;
        QStringList choices, details;
        std::function<int()> value;
        std::function<void(int)> choose;
    };
    // Hosts with settings the library can offer without a book open give them here: rows, and a factory for the
    // front light controls (empty: none). A Settings row in the More sheet opens the page; without either, the row is hidden.
    void setHostSettings(QVector<HostOption> options, std::function<std::unique_ptr<LightControls>(QWidget *)> light);

    std::function<void(const QString &key)> openBook, removeFolder, removeBook;
    std::function<void(const QString &key, Book::Mark mark)> markBook;
    std::function<void()> addFolder, openFile, rescan;
    // Hosts with no system back action offer a way out of the application. Rows whose callback is unset are hidden.
    std::function<void()> exitApp;
    std::function<void(const Settings &settings)> settingsChanged;
    // On Android a book opened on its own is Kobalt's copy, which removal
    // deletes. The confirmation says so.
    bool singleBooksAreCopies = false;

protected:
    void resizeEvent(QResizeEvent *) override;
    void hideEvent(QHideEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;

private:
    const Index &index;
    std::function<QString(const QString &)> coverPath;
    ui::CellButton *search, *more, *closeSearch;
    QVector<ui::CellButton *> modes;
    QLineEdit *query;
    std::unique_ptr<KeyboardController> keyboard;
    QPushButton *sort;
    ui::LoadingIndicator *progress;
    BookPages *pages;
    ui::PagerBar *pager;
    QWidget *empty;
    SortSheet *sortSheet = nullptr;
    MoreSheet *moreSheet = nullptr;
    FolderPage *folderPage = nullptr;
    SettingsPage *settingsPage = nullptr;
    ui::ModalOverlay *overlay = nullptr;
    QTimer *throttle, *clockTimer;
    HeaderStatus lastStatus;
    std::function<HeaderStatus()> statusSource;
    QVector<HostOption> hostOptions;
    std::function<std::unique_ptr<LightControls>(QWidget *)> lightControls;
    bool scanning = false, searching = false, dark = false;
    int found = 0;
    int statusReserve() const;
    bool hasHostSettings() const;
    QRect statusArea() const;
    void paintStatus(QPainter &painter, const QColor &ink, const QColor &paper) const;
    void showSettings();
    void showHostChoice(int option);
    void arrange();
    void rebuild(bool keepPage = true);
    void applySettings(const Settings &settings);
    void showBookMenu(const QString &key);
    void showMoreSheet();
    void showMenu(ui::ActionMenu *menu, const char *name);
    void confirmRemove(const QString &key);
    void showFolders();
    void closeOverlay();
    QWidget *buildEmpty();
    void setSearching(bool value);
};
}
