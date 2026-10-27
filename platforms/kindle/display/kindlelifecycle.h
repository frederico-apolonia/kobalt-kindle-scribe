// Power lifecycle of the Scribe as seen by an application that replaced the Amazon UI. powerd decides when the
// device sleeps; this class only listens (a supervised `lipc-wait-event -m com.lab126.powerd ...`) and reports
// the four events KOReader also consumes. Only those names are parsed; anything else is ignored.
// With lab126_gui stopped nobody forwards the power button to powerd (and powerd then ignores it as "splash"), so
// KOBALT_KINDLE_POWERKEY=1 reads the power key node here and a
// press is passed on like KOReader does: `lipc-set-prop -i com.lab126.powerd powerButton 1`.
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QSocketNotifier>
#include <QtCore/QTimer>
#include <memory>

class KindleLifecycle final : public QObject {
    Q_OBJECT
public:
    explicit KindleLifecycle(QObject *parent = nullptr);
    ~KindleLifecycle() override;

    // Parses one output line; public so the grammar can be tested without a process.
    static bool parse(const QByteArray &line, QByteArray *name, int *source);

signals:
    // Button or cover is putting the device to sleep: save, quiesce input. `source` is powerd's code (2 button, 4 hall).
    void goingToSleep(int source);
    // The screen saver ended or the device resumed: repaint everything with a flashing refresh.
    void wokeUp(int source);

private:
    void start();
    void readOutput();
    void openPowerKey();
    void readPowerKey();

    QProcess process;
    QByteArray pending;
    QTimer restart;
    int failures = 0;
    bool stopping = false;
    int keyFd = -1;
    std::unique_ptr<QSocketNotifier> keyNotifier;
};
