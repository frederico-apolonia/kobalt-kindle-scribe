// Kindle Scribe evdev decoders with no Qt or Linux header dependency, so the same code runs in the QPA plugin and
// in a host-side replay of raw captures (tools/replay_input.cpp).
//
// Measured on firmware 5.18.6 (see the research note, Stage 3):
//   touch  pt_mt          MT protocol B, 2 slots, X 0-1859 / Y 0-2479 = framebuffer pixels (identity mapping),
//                         palm contacts carry ABS_MT_TOOL_TYPE == 2 and must be ignored
//   pen    WacomDigitizer X 0-15624 / Y 0-20832 scale linearly onto the framebuffer, pressure 0-4095, tilt in
//                         1/100 degree, hover (BTN_TOOL_PEN without BTN_TOUCH) reports pressure 0
//   accel  kx132-accel    only edge-triggered EV_ABS/ABS_PRESSURE codes 15-18, no X/Y/Z stream
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace kindleinput {

constexpr std::uint16_t kEvSyn = 0, kEvKey = 1, kEvAbs = 3;
constexpr std::uint16_t kSynReport = 0, kSynDropped = 3;
constexpr std::uint16_t kAbsX = 0x00, kAbsY = 0x01, kAbsPressure = 0x18, kAbsDistance = 0x19, kAbsTiltX = 0x1a,
                        kAbsTiltY = 0x1b;
constexpr std::uint16_t kAbsMtSlot = 0x2f, kAbsMtPositionX = 0x35, kAbsMtPositionY = 0x36, kAbsMtToolType = 0x37,
                        kAbsMtTrackingId = 0x39, kAbsMtPressure = 0x3a;
constexpr std::uint16_t kBtnToolPen = 0x140, kBtnTouch = 0x14a;
constexpr int kToolPalm = 2;

struct RawEvent {
    std::uint16_t type;
    std::uint16_t code;
    std::int32_t value;
};

// Orientation codes reported on EV_ABS/ABS_PRESSURE by the accelerometer (kx132 driver).
enum class Orientation { Unknown, PortraitUp, PortraitDown, LandscapeTopLeft, LandscapeTopRight };

inline Orientation orientationFromCode(int code)
{
    switch (code) {
    case 15: return Orientation::PortraitUp;        // upright
    case 16: return Orientation::PortraitDown;      // upside down
    case 17: return Orientation::LandscapeTopLeft;  // top edge toward the viewer's left
    case 18: return Orientation::LandscapeTopRight; // top edge toward the viewer's right
    default: return Orientation::Unknown;
    }
}

// MT protocol B touch decoder. feed() returns true at SYN_REPORT when the frame holds changes; `points` then lists
// every contact that is down (Pressed on its first frame, then Moved/Stationary) and the ones that just lifted.
class TouchDecoder {
public:
    enum class State { Pressed, Moved, Stationary, Released };
    struct Point {
        int slot;
        int id;
        int x;
        int y;
        int pressure;
        State state;
        bool palmCancel = false; // Released because the controller flagged the contact as a palm after it began
    };

    bool feed(const RawEvent &event, std::vector<Point> &points)
    {
        points.clear();
        if (event.type == kEvAbs)
            absolute(event);
        else if (event.type == kEvSyn && event.code == kSynDropped)
            dropped = true;
        else if (event.type == kEvSyn && event.code == kSynReport)
            return report(points);
        return false;
    }

    // Lift every forwarded contact on the next SYN_REPORT (after a lost sync).
    void reset() { dropped = true; }

    // True while a contact is known to the consumer: forwarded and not yet released.
    bool anyForwarded() const
    {
        for (const Slot &s : table) {
            if (s.forwarded)
                return true;
        }
        return false;
    }

    // The consumer discarded every contact (a touch cancel): forget them without reporting. Contacts that are
    // still down stay unknown, and so ignored, until they lift.
    void clear()
    {
        for (Slot &s : table)
            s = Slot{};
        dropped = false;
    }

private:
    static constexpr int kSlots = 16;
    struct Slot {
        bool active = false;
        bool forwarded = false;
        bool starting = false;
        bool ending = false;
        bool moved = false;
        bool hasX = false;
        bool hasY = false;
        int id = -1;
        int x = 0;
        int y = 0;
        int pressure = 0;
        int tool = 0;
    };

    Slot *slot() { return current >= 0 && current < kSlots ? &table[std::size_t(current)] : nullptr; }

    void absolute(const RawEvent &event)
    {
        if (event.code == kAbsMtSlot) {
            current = event.value;
            return;
        }
        Slot *s = slot();
        if (!s)
            return;
        switch (event.code) {
        case kAbsMtTrackingId:
            if (event.value < 0) {
                s->ending = true;
            } else {
                s->starting = true;
                s->id = event.value;
                s->tool = 0; // a tool type event later in the same frame sets palm
                s->hasX = false;
                s->hasY = false;
            }
            break;
        case kAbsMtPositionX: s->x = event.value; s->hasX = true; s->moved = true; break;
        case kAbsMtPositionY: s->y = event.value; s->hasY = true; s->moved = true; break;
        case kAbsMtPressure: s->pressure = event.value; break;
        case kAbsMtToolType: s->tool = event.value; break;
        default: break;
        }
    }

    bool report(std::vector<Point> &points)
    {
        if (dropped) {
            for (int i = 0; i < kSlots; ++i) {
                Slot &s = table[std::size_t(i)];
                if (s.forwarded)
                    points.push_back({i, s.id, s.x, s.y, s.pressure, State::Released});
                s = Slot{};
            }
            dropped = false;
            return !points.empty();
        }
        for (int i = 0; i < kSlots; ++i) {
            Slot &s = table[std::size_t(i)];
            if (s.ending) {
                if (s.forwarded)
                    points.push_back({i, s.id, s.x, s.y, s.pressure, State::Released});
                s = Slot{};
                continue;
            }
            if (s.starting && !s.active)
                s.active = true;
            if (!s.active)
                continue;
            if (s.tool == kToolPalm) {
                if (s.forwarded) {
                    points.push_back({i, s.id, s.x, s.y, s.pressure, State::Released, true});
                    s.forwarded = false;
                }
            } else if (s.hasX && s.hasY) {
                const State state = !s.forwarded ? State::Pressed : (s.moved ? State::Moved : State::Stationary);
                s.forwarded = true;
                points.push_back({i, s.id, s.x, s.y, s.pressure, state});
            }
            s.starting = false;
            s.moved = false;
        }
        return !points.empty();
    }

    std::array<Slot, kSlots> table{}; // not "slots": Qt defines that as a keyword macro
    int current = 0;
    bool dropped = false;
};

// Single-pointer Wacom decoder. One frame per SYN_REPORT while the pen is, or just was, in proximity.
class PenDecoder {
public:
    struct Frame {
        bool enter = false;     // BTN_TOOL_PEN rose
        bool leave = false;     // BTN_TOOL_PEN fell
        bool press = false;     // BTN_TOUCH rose
        bool release = false;   // BTN_TOUCH fell
        bool touching = false;  // contact now
        bool proximity = false; // in range now
        int x = 0;
        int y = 0;
        int pressure = 0;
        int tiltX = 0;
        int tiltY = 0;
        int distance = 0;
    };

    bool feed(const RawEvent &event, Frame &frame)
    {
        if (event.type == kEvAbs) {
            switch (event.code) {
            case kAbsX: x = event.value; break;
            case kAbsY: y = event.value; break;
            case kAbsPressure: pressure = event.value; break;
            case kAbsDistance: distance = event.value; break;
            case kAbsTiltX: tiltX = event.value; break;
            case kAbsTiltY: tiltY = event.value; break;
            default: break;
            }
        } else if (event.type == kEvKey) {
            if (event.code == kBtnToolPen)
                pen = event.value != 0;
            else if (event.code == kBtnTouch)
                touch = event.value != 0;
        } else if (event.type == kEvSyn && event.code == kSynDropped) {
            lost = true;
        } else if (event.type == kEvSyn && event.code == kSynReport) {
            return report(frame);
        }
        return false;
    }

    void reset() { lost = true; }

private:
    bool report(Frame &frame)
    {
        if (lost) {
            lost = false;
            pen = false;
            touch = false;
        }
        const bool nowTouch = touch && pen;
        frame = Frame{};
        frame.enter = pen && !wasPen;
        frame.leave = !pen && wasPen;
        frame.press = nowTouch && !wasTouch;
        frame.release = !nowTouch && wasTouch;
        frame.touching = nowTouch;
        frame.proximity = pen;
        frame.x = x;
        frame.y = y;
        frame.pressure = nowTouch ? pressure : 0;
        frame.tiltX = tiltX;
        frame.tiltY = tiltY;
        frame.distance = distance;
        const bool wanted = pen || wasPen; // not "emit": Qt keyword macro
        wasPen = pen;
        wasTouch = nowTouch;
        return wanted;
    }

    int x = 0, y = 0, pressure = 0, tiltX = 0, tiltY = 0, distance = 0;
    bool pen = false, touch = false, wasPen = false, wasTouch = false, lost = false;
};

} // namespace kindleinput
