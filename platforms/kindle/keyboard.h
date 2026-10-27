#pragma once
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <vector>

class QLineEdit;

namespace kobalt::kindle {
// An on-screen keyboard drawn by Kobalt itself: the Kindle has no system keyboard. It never takes focus, so the
// editor keeps its caret and selection; keys insert into the attached editor. It docks to the bottom of its
// parent, which is the host window for the library and the search panel inside a reader.
class OnScreenKeyboard final : public QWidget {
public:
    explicit OnScreenKeyboard(QWidget *host);
    // Shows the keyboard for `editor`. The keyboard stays hidden until this is called.
    void attach(QLineEdit *editor);
    // Hides the keyboard. The editor keeps its focus.
    void dismiss();
    // Docks the keyboard to the bottom of its parent again, after the parent changed size.
    void place();
    QLineEdit *target() const {
        return editor;
    }
    // Test seam: taps the key labelled `label` (a character, "<?123>", "<abc>", "<áé>", "<shift>", "<back>", "<space>" or
    // "<enter>") on the current layer, through the same mouse events a finger produces. False if there is no such key.
    bool tap(const QString &label);
    // Called by the Enter key. Without it, Enter sends a Return key press to the editor.
    std::function<void()> submitted;

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Layer { Letters, Symbols, Accents };
    enum class Shift { Off, Once, Caps };
    enum class Role { Character, Space, Shift, Backspace, Enter, ToLayer, Gap };
    struct Spec {
        Role role;
        QString text;
        qreal weight = 1;
        Layer target = Layer::Letters;
    };
    struct Key {
        QRect area;
        Role role;
        QString text;
        Layer target;
    };
    QPointer<QLineEdit> editor;
    Layer layer = Layer::Letters;
    Shift shift = Shift::Off;
    std::vector<Key> keys;
    int pressed = -1;
    QTimer repeat;

    std::vector<std::vector<Spec>> specs() const;
    void buildKeys();
    int keyAt(const QPoint &point) const;
    void activate(const Key &key);
    void erase();
    QString shown(const QString &text) const;
    int stripHeight() const;
    int keyHeight() const;
    int gap() const;
    int preferredHeight() const;
};
}
