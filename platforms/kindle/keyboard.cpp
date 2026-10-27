#include "keyboard.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "interfaces/reader/ui/style/scale.h"
#include "support/strings.h"
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <utility>

namespace kobalt::kindle {
namespace {
constexpr int kRowCount = 4;
// Every row is ten units wide; keys take their weight of it.
constexpr qreal kRowUnits = 10;
QColor mix(const QColor &paper, const QColor &ink, qreal amount) {
    return QColor::fromRgbF(paper.redF() + (ink.redF() - paper.redF()) * amount,
                            paper.greenF() + (ink.greenF() - paper.greenF()) * amount,
                            paper.blueF() + (ink.blueF() - paper.blueF()) * amount);
}
}

OnScreenKeyboard::OnScreenKeyboard(QWidget *host) : QWidget(host)
{
    setObjectName("kindleKeyboard");
    setFocusPolicy(Qt::NoFocus);
    setAutoFillBackground(true);
    hide();
    repeat.setSingleShot(false);
    QObject::connect(&repeat, &QTimer::timeout, this, [this] {
        repeat.setInterval(110);
        erase();
    });
}

int OnScreenKeyboard::stripHeight() const
{
    return ui::px(64);
}
int OnScreenKeyboard::keyHeight() const
{
    return ui::px(112);
}
int OnScreenKeyboard::gap() const
{
    return ui::px(10);
}
int OnScreenKeyboard::preferredHeight() const
{
    return stripHeight() + kRowCount * (keyHeight() + gap()) + gap();
}

void OnScreenKeyboard::attach(QLineEdit *next)
{
    if (!next)
        return;
    if (editor != next) {
        if (editor)
            editor->removeEventFilter(this);
        editor = next;
        editor->installEventFilter(this);
        layer = Layer::Letters;
        shift = Shift::Off;
        buildKeys();
    }
    place();
    show();
    raise();
}

void OnScreenKeyboard::dismiss()
{
    repeat.stop();
    pressed = -1;
    hide();
}

void OnScreenKeyboard::place()
{
    auto *host = parentWidget();
    if (!host)
        return;
    const int height = preferredHeight();
    setGeometry(0, host->height() - height, host->width(), height);
    raise();
}

bool OnScreenKeyboard::eventFilter(QObject *watched, QEvent *event)
{
    // Dismissed with the handle, the keyboard returns when the editor is tapped again.
    if (watched == editor && event->type() == QEvent::MouseButtonRelease && !isVisible())
        attach(editor);
    return QWidget::eventFilter(watched, event);
}

bool OnScreenKeyboard::tap(const QString &label)
{
    const auto named = [](Role role) {
        return role == Role::Shift ? QStringLiteral("<shift>") : role == Role::Backspace ? QStringLiteral("<back>")
             : role == Role::Space ? QStringLiteral("<space>") : role == Role::Enter ? QStringLiteral("<enter>") : QString();
    };
    const QString inner = label.size() > 2 && label.startsWith('<') && label.endsWith('>') ? label.mid(1, label.size() - 2) : label;
    for (const Key &key : keys) {
        if (key.text != label && key.text != inner && named(key.role) != label)
            continue;
        const QPointF center = QRectF(key.area).center();
        QMouseEvent press(QEvent::MouseButtonPress, center, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, center, center, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(this, &press);
        QCoreApplication::sendEvent(this, &release);
        return true;
    }
    return false;
}

std::vector<std::vector<OnScreenKeyboard::Spec>> OnScreenKeyboard::specs() const
{
    std::vector<std::vector<Spec>> rows(kRowCount);
    const auto characters = [this](std::vector<Spec> &row, const QString &text) {
        for (const QChar letter : text)
            row.push_back({Role::Character, QString(letter)});
    };
    switch (layer) {
    case Layer::Letters:
        characters(rows[0], QStringLiteral("qwertyuiop"));
        rows[1].push_back({Role::Gap, {}, .5});
        characters(rows[1], QStringLiteral("asdfghjkl"));
        rows[1].push_back({Role::Gap, {}, .5});
        rows[2].push_back({Role::Shift, {}, 1.5});
        characters(rows[2], QStringLiteral("zxcvbnm"));
        rows[2].push_back({Role::Backspace, {}, 1.5});
        break;
    case Layer::Symbols:
        characters(rows[0], QStringLiteral("1234567890"));
        characters(rows[1], QStringLiteral("-/:;()€&@\""));
        characters(rows[2], QStringLiteral("!?'#%+=_"));
        rows[2].push_back({Role::Backspace, {}, 2});
        break;
    case Layer::Accents:
        characters(rows[0], QStringLiteral("áàâãäåæçñß"));
        characters(rows[1], QStringLiteral("éèêëíìîïóò"));
        rows[2].push_back({Role::Shift, {}, 1.5});
        characters(rows[2], QStringLiteral("ôõöúùûü"));
        rows[2].push_back({Role::Backspace, {}, 1.5});
        break;
    }
    auto &bottom = rows[3];
    bottom.push_back({Role::ToLayer, layer == Layer::Symbols ? QStringLiteral("abc") : QStringLiteral("?123"), 1.5,
                      layer == Layer::Symbols ? Layer::Letters : Layer::Symbols});
    bottom.push_back({Role::ToLayer, layer == Layer::Accents ? QStringLiteral("abc") : QStringLiteral("áé"), 1.5,
                      layer == Layer::Accents ? Layer::Letters : Layer::Accents});
    bottom.push_back({Role::Character, QStringLiteral(",")});
    bottom.push_back({Role::Space, {}, 3});
    bottom.push_back({Role::Character, QStringLiteral(".")});
    bottom.push_back({Role::Enter, strings::get("search_panel.search"), 2});
    return rows;
}

void OnScreenKeyboard::buildKeys()
{
    keys.clear();
    pressed = -1;
    const auto rows = specs();
    const int margin = gap();
    const qreal unit = (width() - 2 * margin) / kRowUnits;
    for (size_t r = 0; r < rows.size(); ++r) {
        const int top = stripHeight() + gap() + int(r) * (keyHeight() + gap());
        qreal x = margin;
        for (const auto &spec : rows[r]) {
            const qreal slot = unit * spec.weight;
            if (spec.role != Role::Gap) {
                const QRect area(qRound(x + gap() / 2.0), top, qRound(slot) - gap(), keyHeight());
                keys.push_back({area, spec.role, spec.text, spec.target});
            }
            x += slot;
        }
    }
}

int OnScreenKeyboard::keyAt(const QPoint &point) const
{
    for (size_t i = 0; i < keys.size(); ++i) {
        // Spread the gaps between keys, so a tap between two keys still lands on the nearer one.
        if (keys[i].area.adjusted(-gap() / 2, -gap() / 2, gap() / 2, gap() / 2).contains(point))
            return int(i);
    }
    return -1;
}

QString OnScreenKeyboard::shown(const QString &text) const
{
    if (shift == Shift::Off || layer == Layer::Symbols || text == QLatin1String("ß"))
        return text;
    return text.toUpper();
}

void OnScreenKeyboard::erase()
{
    if (!editor)
        return;
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Backspace, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &press);
    QCoreApplication::sendEvent(editor, &release);
}

void OnScreenKeyboard::activate(const Key &key)
{
    switch (key.role) {
    case Role::Character:
        if (editor)
            editor->insert(shown(key.text));
        if (shift == Shift::Once) {
            shift = Shift::Off;
            update();
        }
        break;
    case Role::Space:
        if (editor)
            editor->insert(QStringLiteral(" "));
        break;
    case Role::Shift:
        shift = shift == Shift::Off ? Shift::Once : shift == Shift::Once ? Shift::Caps : Shift::Off;
        update();
        break;
    case Role::Backspace:
        break;
    case Role::Enter:
        if (submitted) {
            submitted();
        } else if (editor) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(editor, &press);
            QCoreApplication::sendEvent(editor, &release);
            dismiss();
        }
        break;
    case Role::ToLayer:
        layer = key.target;
        shift = Shift::Off;
        buildKeys();
        update();
        break;
    case Role::Gap:
        break;
    }
}

void OnScreenKeyboard::resizeEvent(QResizeEvent *)
{
    buildKeys();
}

void OnScreenKeyboard::mousePressEvent(QMouseEvent *event)
{
    const QPoint point = event->position().toPoint();
    if (point.y() < stripHeight()) {
        pressed = -2;
        return;
    }
    pressed = keyAt(point);
    if (pressed < 0)
        return;
    update(keys[size_t(pressed)].area.adjusted(-2, -2, 2, 2));
    if (keys[size_t(pressed)].role == Role::Backspace) {
        erase();
        repeat.setInterval(450);
        repeat.start();
    }
}

void OnScreenKeyboard::mouseReleaseEvent(QMouseEvent *event)
{
    repeat.stop();
    const int was = std::exchange(pressed, -1);
    const QPoint point = event->position().toPoint();
    if (was == -2) {
        if (point.y() < stripHeight())
            dismiss();
        return;
    }
    if (was < 0)
        return;
    const Key key = keys[size_t(was)];
    update(key.area.adjusted(-2, -2, 2, 2));
    if (keyAt(point) == was)
        activate(key);
}

void OnScreenKeyboard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor paper = palette().color(QPalette::Window);
    const QColor ink = palette().color(QPalette::WindowText);
    painter.fillRect(rect(), paper);
    painter.fillRect(0, 0, width(), ui::px(3), ink);

    // The handle: tapping the strip hides the keyboard.
    const QPointF middle(width() / 2.0, stripHeight() / 2.0 + ui::px(2));
    const qreal reach = ui::px(26), drop = ui::px(10);
    painter.setPen(QPen(ink, ui::px(5), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolyline(QPolygonF{middle + QPointF(-reach, -drop), middle + QPointF(0, drop),
                                   middle + QPointF(reach, -drop)});

    const QFont font = ui::interfaceFont(ui::px(46));
    const qreal scale = keyHeight() * .26;
    for (size_t i = 0; i < keys.size(); ++i) {
        const Key &key = keys[i];
        const bool functional = key.role != Role::Character && key.role != Role::Space;
        const bool inverted = int(i) == pressed || key.role == Role::Enter ||
                              (key.role == Role::Shift && shift != Shift::Off);
        const QColor fill = inverted ? ink : functional ? mix(paper, ink, .12) : paper;
        const QColor text = inverted ? paper : ink;
        painter.setPen(QPen(ink, ui::px(2)));
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(key.area).adjusted(1, 1, -1, -1), ui::px(10), ui::px(10));
        painter.setPen(QPen(text, ui::px(4), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.setFont(font);
        const QPointF center = QRectF(key.area).center();
        switch (key.role) {
        case Role::Shift: {
            painter.save();
            painter.translate(center);
            if (shift == Shift::Caps)
                painter.setBrush(text);
            painter.drawPolygon(QPolygonF{{0, -scale}, {scale, 0}, {scale * .45, 0}, {scale * .45, scale},
                                          {-scale * .45, scale}, {-scale * .45, 0}, {-scale, 0}});
            painter.restore();
            break;
        }
        case Role::Backspace: {
            painter.save();
            painter.translate(center);
            const qreal w = scale * 1.4, h = scale * .9, cross = scale * .35;
            painter.drawPolygon(QPolygonF{{-w, 0}, {-w * .35, -h}, {w, -h}, {w, h}, {-w * .35, h}});
            painter.drawLine(QPointF(-cross + w * .2, -cross), QPointF(cross + w * .2, cross));
            painter.drawLine(QPointF(-cross + w * .2, cross), QPointF(cross + w * .2, -cross));
            painter.restore();
            break;
        }
        case Role::Space:
            break;
        case Role::Character:
            painter.drawText(key.area, Qt::AlignCenter, shown(key.text));
            break;
        case Role::ToLayer:
        case Role::Enter:
            painter.drawText(key.area, Qt::AlignCenter, key.text);
            break;
        case Role::Gap:
            break;
        }
    }
}
}
