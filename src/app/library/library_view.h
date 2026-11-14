#pragma once
#include "ui/controls/panels.h"
#include "library/library_index.h"
#include "platform/api/keyboard_controller.h"
#include <functional>
#include <memory>

class QLineEdit;
class QPainter;
class QPushButton;
class QShowEvent;
class QTimer;

namespace kobalt {
class SavedWords;
class WordList;
class WordPage;
struct SavedWord;
} // namespace kobalt

namespace kobalt::ui {
class CellButton;
class CaptionDropdown;
class PagerBar;
class ModalOverlay;
class ActionMenu;
class LoadingIndicator;
} // namespace kobalt::ui

namespace kobalt {
class LightControls;
} // namespace kobalt

namespace kobalt::library {
class AboutScreen;
class BookPages;
class FolderPage;
class SettingsPage;
class SortSheet;

// The Android and desktop library. It draws in device pixels on the reader's
// canvas, with the reader's header, rows, pager and dialogs. The host owns the
// index and every file operation; the view only reports what was chosen.
class LibraryView final : public ui::Panel
{
public:
    LibraryView(const Index &index, std::function<QString(const QString &)> coverPath,
                QWidget *parent = nullptr);
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
    struct HeaderStatus
    {
        QString time;
        // Percent; negative when unknown, which leaves out the battery.
        int battery = -1;
        bool charging = false;
        bool operator==(const HeaderStatus &other) const
        {
            return time == other.time && battery == other.battery && charging == other.charging;
        }
    };
    using HeaderStatusSource = std::function<HeaderStatus()>;
    // `source` is asked for the status when the library is shown and every few seconds while it stays visible.
    void setStatus(HeaderStatusSource source);

    // A row of the Settings page that the host owns. With two or more `choices` it picks one of them, `value`
    // returns the index of the current one, and `details` (optional, parallel to `choices`) explain each. With none
    // it is a switch, and `value` is 0 or 1.
    struct HostOption
    {
        QString name, title, description;
        QStringList choices, details;
        std::function<int()> value;
        std::function<void(int)> choose;
    };
    // Hosts with settings the library can offer without a book open give them here: rows, and a factory for the
    // front light controls (empty: none). A Settings row in the More menu opens the page; without either, the row is
    // hidden.
    void setHostSettings(QVector<HostOption> options,
                         std::function<std::unique_ptr<LightControls>(QWidget *)> light);

    std::function<void(const QString &key)> openBook, removeFolder, removeBook;
    // Opens a book at a saved word's position.
    std::function<void(const QString &key, const QString &position)> openPassage;
    // Adds My Words to the menu. Words live in the profile database there.
    void setWordsDirectory(const QString &directory);
    // The Library book a saved word came from, found by its path first and
    // then by the content fingerprint of books with the same title. Empty when
    // the book is not in the Library.
    QString bookFor(const SavedWord &word) const;
    std::function<void(const QString &key, Book::Mark mark)> markBook;
    std::function<void()> addFolder, openFile, rescan;
    // Hosts with no system back action offer a way out of the application. Rows whose callback is unset are hidden:
    // without addFolder and openFile the More menu offers neither pick; without exitApp it has no Exit row.
    std::function<void()> exitApp;
    std::function<void(const Settings &settings)> settingsChanged;
    // On Android a book opened on its own is Kobalt's copy, which removal
    // deletes. The confirmation says so.
    bool singleBooksAreCopies = false;
    // Optional host folder pickers: dictionaries, fonts, custom pins, in that order.
    std::function<QStringList()> resourceFolderLabels;
    std::function<void(int kind, bool clear)> chooseResourceFolder;

protected:
    void resizeEvent(QResizeEvent *) override;
    void hideEvent(QHideEvent *) override;
    void showEvent(QShowEvent *) override;
    void paintEvent(QPaintEvent *) override;

private:
    const Index &index;
    std::function<QString(const QString &)> coverPath;
    ui::CellButton *search, *more, *closeSearch;
    QVector<ui::CellButton *> modes;
    QLineEdit *query;
    std::unique_ptr<KeyboardController> keyboard;
    ui::CaptionDropdown *sort;
    ui::LoadingIndicator *progress;
    BookPages *pages;
    ui::PagerBar *pager;
    QWidget *empty;
    SortSheet *sortSheet = nullptr;
    ui::ActionMenu *moreSheet = nullptr;
    FolderPage *folderPage = nullptr;
    AboutScreen *about = nullptr;
    std::unique_ptr<SavedWords> savedWords;
    QString savedWordsDirectory;
    bool moreSheetWords = false, moreSheetPickers = true, moreSheetSettings = false, moreSheetExit = false;
    WordList *wordList = nullptr;
    WordPage *wordPage = nullptr;
    void showWords();
    void openWord(const SavedWord &word);
    ui::ModalOverlay *overlay = nullptr;
    QTimer *throttle, *clockTimer;
    SettingsPage *settingsPage = nullptr;
    HeaderStatus lastStatus;
    std::function<HeaderStatus()> statusSource;
    QVector<HostOption> hostOptions;
    std::function<std::unique_ptr<LightControls>(QWidget *)> lightControls;
    bool hasHostSettings() const;
    int statusReserve() const;
    QRect statusArea() const;
    void paintStatus(QPainter &painter, const QColor &ink, const QColor &paper) const;
    void showSettings();
    void showHostChoice(int option);
    bool scanning = false, searching = false, dark = false;
    int found = 0;
    void arrange();
    void rebuild(bool keepPage = true);
    void applySettings(const Settings &settings);
    void showBookMenu(const QString &key);
    void showMoreSheet();
    void showMenu(ui::ActionMenu *menu, const char *name);
    void confirmRemove(const QString &key);
    void showFolders();
    void showResourceFolders();
    void showAbout();
    void closeOverlay();
    QWidget *buildEmpty();
    void setSearching(bool value);
};
} // namespace kobalt::library
