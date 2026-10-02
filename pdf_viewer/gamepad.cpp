#include "gamepad.h"
#include "main_widget.h"
#include "input.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>

#include <qapplication.h>
#include <qcoreevent.h>
#include <qdebug.h>
#include <qevent.h>

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

extern bool GAMEPAD_ENABLED;
extern float GAMEPAD_DEADZONE;
extern float GAMEPAD_ANALOG_SCROLL_SPEED;

namespace {

constexpr int kAnalogTimerIntervalMs = 16; // ~60 Hz

constexpr int kMaxJoystickDevices = 16;
constexpr int kDeviceNameBufferSize = 128;
constexpr char kJoystickDevicePrefix[] = "/dev/input/js";

constexpr int kDisabledSleepMs = 200;
constexpr int kDeviceScanIntervalMs = 1000;
constexpr int kNoDeviceSleepMs = 100;
constexpr int kPollTimeoutMs = 50;
constexpr int kPollErrorSleepMs = 20;

constexpr int kAutoRepeatInitialDelayMs = 250;
constexpr int kAutoRepeatIntervalMs = 55;

constexpr float kRawAxisMaximum = 32767.0f;

constexpr float kDiscreteDeadzoneMin = 0.05f;
constexpr float kDiscreteDeadzoneMax = 0.85f;
constexpr float kSmoothDeadzoneMin = 0.01f;
constexpr float kSmoothDeadzoneMax = 0.90f;
constexpr float kDpadDeadzone = 0.40f;

constexpr int kTriggerPressThreshold = 8000;
constexpr int kTriggerReleaseThreshold = 4000;

constexpr float kAnalogScrollBaseSpeed = 18.0f;


enum GamepadAxisIndex {
    AxisLeftStickX = 0,
    AxisLeftStickY = 1,
    AxisLeftTrigger = 2,
    AxisRightStickX = 3,
    AxisRightStickY = 4,
    AxisRightTrigger = 5,
    AxisDpadX = 6,
    AxisDpadY = 7,
};

constexpr std::array<int, 15> kButtonToKey = {
    Key_Gamepad_A,
    Key_Gamepad_B,
    Key_Gamepad_X,
    Key_Gamepad_Y,
    Key_Gamepad_LB,
    Key_Gamepad_RB,
    Key_Gamepad_Select,
    Key_Gamepad_Start,
    Key_Gamepad_Guide,
    Key_Gamepad_L3,
    Key_Gamepad_R3,
    Key_Gamepad_DpadUp,
    Key_Gamepad_DpadRight,
    Key_Gamepad_DpadDown,
    Key_Gamepad_DpadLeft,
};

float normalize_axis(int value) {
    return std::clamp(
        static_cast<float>(value) / kRawAxisMaximum,
        -1.0f,
        1.0f
    );
}

float normalize_trigger(float normalized_axis) {
    return (normalized_axis + 1.0f) * 0.5f;
}

} // namespace

GamepadManager::GamepadManager(
    std::function<MainWidget*()> active_window_getter,
    QObject* parent
)
    : QObject(parent),
      get_active_window_fn(std::move(active_window_getter)) {

    connect(
        &analog_timer,
        &QTimer::timeout,
        this,
        &GamepadManager::on_analog_tick
    );
    analog_timer.start(kAnalogTimerIntervalMs);

    reader_thread = std::thread([this]() {
        reader_loop();
    });
}

GamepadManager::~GamepadManager() {
    should_quit = true;

    if (reader_thread.joinable()) {
        reader_thread.join();
    }

    close_all_devices();
}

std::string GamepadManager::get_device_name() const {
    return primary_device_name;
}

void GamepadManager::close_all_devices() {
#ifdef __linux__
    for (auto& dev : open_devices) {
        if (dev.fd >= 0) {
            close(dev.fd);
            dev.fd = -1;
        }
    }

    open_devices.clear();
#endif

    primary_device_name.clear();
    is_connected = false;
}

void GamepadManager::scan_for_devices() {
#ifdef __linux__
    for (int i = 0; i < kMaxJoystickDevices; ++i) {
        const std::string candidate =
            std::string(kJoystickDevicePrefix) + std::to_string(i);

        bool already_open = false;
        for (const auto& dev : open_devices) {
            if (dev.path == candidate) {
                already_open = true;
                break;
            }
        }

        if (already_open) {
            continue;
        }

        const int fd = open(candidate.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        char name_buf[kDeviceNameBufferSize] = {0};
        if (ioctl(fd, JSIOCGNAME(sizeof(name_buf)), name_buf) < 0) {
            snprintf(name_buf, sizeof(name_buf), "Gamepad %d", i);
        }

        GamepadDevice dev;
        dev.fd = fd;
        dev.path = candidate;
        dev.name = std::string(name_buf);

        open_devices.push_back(dev);
        primary_device_name = dev.name;
        is_connected = true;
    }

    is_connected = !open_devices.empty();
#endif
}

void GamepadManager::reader_loop() {
#ifdef __linux__
    auto last_scan_time = std::chrono::steady_clock::now();
    scan_for_devices();

    while (!should_quit) {
        if (!GAMEPAD_ENABLED) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kDisabledSleepMs)
            );
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_since_scan =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_scan_time
            ).count();

        if (elapsed_since_scan >= kDeviceScanIntervalMs) {
            scan_for_devices();
            last_scan_time = now;
        }

        if (open_devices.empty()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kNoDeviceSleepMs)
            );
            continue;
        }

        std::vector<struct pollfd> pfds(open_devices.size());
        for (size_t i = 0; i < open_devices.size(); ++i) {
            pfds[i].fd = open_devices[i].fd;
            pfds[i].events = POLLIN;
            pfds[i].revents = 0;
        }

        const int ret = poll(pfds.data(), pfds.size(), kPollTimeoutMs);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(kPollErrorSleepMs)
            );
            continue;
        }

        if (ret == 0) {
            continue;
        }

        std::vector<int> to_remove;

        for (size_t i = 0; i < pfds.size(); ++i) {
            if (pfds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                close(open_devices[i].fd);
                to_remove.push_back(static_cast<int>(i));
                continue;
            }

            if (!(pfds[i].revents & POLLIN)) {
                continue;
            }

            struct js_event ev;
            bool read_error = false;

            while (true) {
                const ssize_t bytes =
                    read(open_devices[i].fd, &ev, sizeof(ev));

                if (bytes == sizeof(ev)) {
                    // Ignore synthetic initialization events to avoid
                    // spurious actions.
                    if (ev.type & JS_EVENT_INIT) {
                        continue;
                    }

                    if (ev.type == JS_EVENT_BUTTON) {
                        process_button_event(ev.number, ev.value != 0);
                    }
                    else if (ev.type == JS_EVENT_AXIS) {
                        process_axis_event(ev.number, ev.value);
                    }
                }
                else if (bytes < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }

                    if (errno == EINTR) {
                        continue;
                    }

                    // Fatal read error, for example ENODEV, EBADF or EIO.
                    read_error = true;
                    break;
                }
                else {
                    // EOF: device disconnected.
                    read_error = true;
                    break;
                }
            }

            if (read_error) {
                close(open_devices[i].fd);
                to_remove.push_back(static_cast<int>(i));
            }
        }

        if (!to_remove.empty()) {
            for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
                open_devices.erase(open_devices.begin() + *it);
            }

            is_connected = !open_devices.empty();
        }
    }
#endif
}

void GamepadManager::process_button_event(int button_index, bool pressed) {
    if (button_index < 0 ||
        static_cast<size_t>(button_index) >= kButtonToKey.size()) {
        return;
    }

    dispatch_key(kButtonToKey[static_cast<size_t>(button_index)], pressed);
}

void GamepadManager::update_axis_direction(
    DirectionRepeatState& state,
    int neg_key,
    int pos_key,
    float norm,
    float deadzone
) {
    int target_key = 0;

    if (norm < -deadzone) {
        target_key = neg_key;
    }
    else if (norm > deadzone) {
        target_key = pos_key;
    }

    const auto now = std::chrono::steady_clock::now();

    if (target_key == state.active_key) {
        return;
    }

    if (state.active_key != 0) {
        dispatch_key(state.active_key, false, false);
    }

    state.active_key = target_key;

    if (target_key != 0) {
        state.is_pressed = true;
        state.press_time = now;
        state.last_repeat_time = now;
        dispatch_key(target_key, true, false);
    }
    else {
        state.is_pressed = false;
    }
}

void GamepadManager::check_auto_repeat(
    DirectionRepeatState& state,
    const std::chrono::steady_clock::time_point& now
) {
    if (!state.is_pressed || state.active_key == 0) {
        return;
    }

    const auto held_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state.press_time
        ).count();

    const auto repeat_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state.last_repeat_time
        ).count();

    if (held_ms >= kAutoRepeatInitialDelayMs &&
        repeat_ms >= kAutoRepeatIntervalMs) {
        state.last_repeat_time = now;
        dispatch_key(state.active_key, true, true);
    }
}

void GamepadManager::process_axis_event(int axis_index, int value) {
    const float norm = normalize_axis(value);
    const float deadzone = std::clamp(
        GAMEPAD_DEADZONE,
        kDiscreteDeadzoneMin,
        kDiscreteDeadzoneMax
    );

    switch (axis_index) {
        case AxisLeftStickX:
            axis_left_x.store(norm);
            update_axis_direction(
                ls_x_state,
                Key_Gamepad_LS_Left,
                Key_Gamepad_LS_Right,
                norm,
                deadzone
            );
            break;

        case AxisLeftStickY:
            axis_left_y.store(norm);
            update_axis_direction(
                ls_y_state,
                Key_Gamepad_LS_Up,
                Key_Gamepad_LS_Down,
                norm,
                deadzone
            );
            break;

        case AxisLeftTrigger:
            axis_lt.store(normalize_trigger(norm));

            if (value > kTriggerPressThreshold && !lt_pressed) {
                lt_pressed = true;
                dispatch_key(Key_Gamepad_LT, true);
            }
            else if (value < kTriggerReleaseThreshold && lt_pressed) {
                lt_pressed = false;
                dispatch_key(Key_Gamepad_LT, false);
            }
            break;

        case AxisRightStickX:
            axis_right_x.store(norm);
            update_axis_direction(
                rs_x_state,
                Key_Gamepad_RS_Left,
                Key_Gamepad_RS_Right,
                norm,
                deadzone
            );
            break;

        case AxisRightStickY:
            axis_right_y.store(norm);
            update_axis_direction(
                rs_y_state,
                Key_Gamepad_RS_Up,
                Key_Gamepad_RS_Down,
                norm,
                deadzone
            );
            break;

        case AxisRightTrigger:
            axis_rt.store(normalize_trigger(norm));

            if (value > kTriggerPressThreshold && !rt_pressed) {
                rt_pressed = true;
                dispatch_key(Key_Gamepad_RT, true);
            }
            else if (value < kTriggerReleaseThreshold && rt_pressed) {
                rt_pressed = false;
                dispatch_key(Key_Gamepad_RT, false);
            }
            break;

        case AxisDpadX:
            update_axis_direction(
                dpad_x_state,
                Key_Gamepad_DpadLeft,
                Key_Gamepad_DpadRight,
                norm,
                kDpadDeadzone
            );
            break;

        case AxisDpadY:
            update_axis_direction(
                dpad_y_state,
                Key_Gamepad_DpadUp,
                Key_Gamepad_DpadDown,
                norm,
                kDpadDeadzone
            );
            break;

        default:
            break;
    }
}

void GamepadManager::dispatch_key(
    int key_code,
    bool pressed,
    bool auto_repeat
) {
    if (!GAMEPAD_ENABLED) {
        return;
    }

    QMetaObject::invokeMethod(
        this,
        [this, key_code, pressed, auto_repeat]() {
            MainWidget* target =
                get_active_window_fn ? get_active_window_fn() : nullptr;

            QWidget* dest = QApplication::focusWidget();
            if (!dest) {
                dest = target;
            }

            QKeyEvent* ev = new QKeyEvent(
                pressed ? QEvent::KeyPress : QEvent::KeyRelease,
                key_code,
                Qt::NoModifier,
                QString(),
                auto_repeat
            );

            QCoreApplication::postEvent(dest, ev);
        },
        Qt::QueuedConnection
    );
}
void GamepadManager::on_analog_tick() {
    if (!GAMEPAD_ENABLED) {
        return;
    }

    MainWidget* target =
        get_active_window_fn ? get_active_window_fn() : nullptr;

    if (!target) {
        return;
    }

    const bool in_menu = target->has_active_menu_widget();
    const auto now = std::chrono::steady_clock::now();

    // In menus, sticks and d-pad always dispatch discrete navigation events with auto-repeat.
    if (in_menu) {
        check_auto_repeat(ls_x_state, now);
        check_auto_repeat(ls_y_state, now);
        check_auto_repeat(rs_x_state, now);
        check_auto_repeat(rs_y_state, now);
        check_auto_repeat(dpad_x_state, now);
        check_auto_repeat(dpad_y_state, now);
        return;
    }

    // Auto-repeat for d-pad (always discrete)
    check_auto_repeat(dpad_x_state, now);
    check_auto_repeat(dpad_y_state, now);

    InputHandler* ih = target->input_handler;
    if (!ih) {
        return;
    }

    const float deadzone = std::clamp(
        GAMEPAD_DEADZONE,
        kSmoothDeadzoneMin,
        kSmoothDeadzoneMax
    );

    const bool ls_all_smooth = ih->is_key_bound(Key_Gamepad_LS, "smooth_scroll");
    const bool rs_all_smooth = ih->is_key_bound(Key_Gamepad_RS, "smooth_scroll");

    const bool ls_y_smooth = ls_all_smooth ||
        ih->is_key_bound(Key_Gamepad_LS_Down, "smooth_scroll_down") ||
        ih->is_key_bound(Key_Gamepad_LS_Down, "smooth_scroll_up") ||
        ih->is_key_bound(Key_Gamepad_LS_Up, "smooth_scroll_up") ||
        ih->is_key_bound(Key_Gamepad_LS_Up, "smooth_scroll_down");

    const bool ls_x_smooth = ls_all_smooth ||
        ih->is_key_bound(Key_Gamepad_LS_Left, "smooth_scroll_left") ||
        ih->is_key_bound(Key_Gamepad_LS_Left, "smooth_scroll_right") ||
        ih->is_key_bound(Key_Gamepad_LS_Right, "smooth_scroll_right") ||
        ih->is_key_bound(Key_Gamepad_LS_Right, "smooth_scroll_left");

    const bool rs_y_smooth = rs_all_smooth ||
        ih->is_key_bound(Key_Gamepad_RS_Down, "smooth_scroll_down") ||
        ih->is_key_bound(Key_Gamepad_RS_Down, "smooth_scroll_up") ||
        ih->is_key_bound(Key_Gamepad_RS_Up, "smooth_scroll_up") ||
        ih->is_key_bound(Key_Gamepad_RS_Up, "smooth_scroll_down");

    const bool rs_x_smooth = rs_all_smooth ||
        ih->is_key_bound(Key_Gamepad_RS_Left, "smooth_scroll_left") ||
        ih->is_key_bound(Key_Gamepad_RS_Left, "smooth_scroll_right") ||
        ih->is_key_bound(Key_Gamepad_RS_Right, "smooth_scroll_right") ||
        ih->is_key_bound(Key_Gamepad_RS_Right, "smooth_scroll_left");

    // Only auto-repeat discrete keys outside menu when an axis is NOT bound to smooth scroll
    if (!ls_x_smooth) {
        check_auto_repeat(ls_x_state, now);
    }
    if (!ls_y_smooth) {
        check_auto_repeat(ls_y_state, now);
    }
    if (!rs_x_smooth) {
        check_auto_repeat(rs_x_state, now);
    }
    if (!rs_y_smooth) {
        check_auto_repeat(rs_y_state, now);
    }

    if (!target->main_document_view_has_document()) {
        return;
    }

    float dy = 0.0f;
    float dx = 0.0f;

    auto get_deflection = [deadzone](float val) -> float {
        const float a = std::abs(val);
        if (a <= deadzone) return 0.0f;
        return (a - deadzone) / (1.0f - deadzone);
    };

    const float ly = axis_left_y.load();
    const float lx = axis_left_x.load();
    const float ry = axis_right_y.load();
    const float rx = axis_right_x.load();
    const float lt = axis_lt.load();
    const float rt = axis_rt.load();

    // Left Stick Y deflection (strictly declarative: NO fallback defaults)
    if (ly > deadzone) {
        const float d = get_deflection(ly);
        if (ls_all_smooth || ih->is_key_bound(Key_Gamepad_LS_Down, "smooth_scroll_down")) {
            dy += d;
        } else if (ih->is_key_bound(Key_Gamepad_LS_Down, "smooth_scroll_up")) {
            dy -= d;
        }
    } else if (ly < -deadzone) {
        const float d = get_deflection(ly);
        if (ls_all_smooth || ih->is_key_bound(Key_Gamepad_LS_Up, "smooth_scroll_up")) {
            dy -= d;
        } else if (ih->is_key_bound(Key_Gamepad_LS_Up, "smooth_scroll_down")) {
            dy += d;
        }
    }

    // Left Stick X deflection (strictly declarative: NO fallback defaults)
    if (lx > deadzone) {
        const float d = get_deflection(lx);
        if (ls_all_smooth || ih->is_key_bound(Key_Gamepad_LS_Right, "smooth_scroll_right")) {
            dx += d;
        } else if (ih->is_key_bound(Key_Gamepad_LS_Right, "smooth_scroll_left")) {
            dx -= d;
        }
    } else if (lx < -deadzone) {
        const float d = get_deflection(lx);
        if (ls_all_smooth || ih->is_key_bound(Key_Gamepad_LS_Left, "smooth_scroll_left")) {
            dx -= d;
        } else if (ih->is_key_bound(Key_Gamepad_LS_Left, "smooth_scroll_right")) {
            dx += d;
        }
    }

    // Right Stick Y deflection (strictly declarative: NO fallback defaults)
    if (ry > deadzone) {
        const float d = get_deflection(ry);
        if (rs_all_smooth || ih->is_key_bound(Key_Gamepad_RS_Down, "smooth_scroll_down")) {
            dy += d;
        } else if (ih->is_key_bound(Key_Gamepad_RS_Down, "smooth_scroll_up")) {
            dy -= d;
        }
    } else if (ry < -deadzone) {
        const float d = get_deflection(ry);
        if (rs_all_smooth || ih->is_key_bound(Key_Gamepad_RS_Up, "smooth_scroll_up")) {
            dy -= d;
        } else if (ih->is_key_bound(Key_Gamepad_RS_Up, "smooth_scroll_down")) {
            dy += d;
        }
    }

    // Right Stick X deflection (strictly declarative: NO fallback defaults)
    if (rx > deadzone) {
        const float d = get_deflection(rx);
        if (rs_all_smooth || ih->is_key_bound(Key_Gamepad_RS_Right, "smooth_scroll_right")) {
            dx += d;
        } else if (ih->is_key_bound(Key_Gamepad_RS_Right, "smooth_scroll_left")) {
            dx -= d;
        }
    } else if (rx < -deadzone) {
        const float d = get_deflection(rx);
        if (rs_all_smooth || ih->is_key_bound(Key_Gamepad_RS_Left, "smooth_scroll_left")) {
            dx -= d;
        } else if (ih->is_key_bound(Key_Gamepad_RS_Left, "smooth_scroll_right")) {
            dx += d;
        }
    }

    // Triggers (LT / RT) (strictly declarative)
    if (lt > deadzone) {
        const float d = get_deflection(lt);
        if (ih->is_key_bound(Key_Gamepad_LT, "smooth_scroll_up")) {
            dy -= d;
        } else if (ih->is_key_bound(Key_Gamepad_LT, "smooth_scroll_down")) {
            dy += d;
        }
    }
    if (rt > deadzone) {
        const float d = get_deflection(rt);
        if (ih->is_key_bound(Key_Gamepad_RT, "smooth_scroll_down")) {
            dy += d;
        } else if (ih->is_key_bound(Key_Gamepad_RT, "smooth_scroll_up")) {
            dy -= d;
        }
    }

    if (dy == 0.0f && dx == 0.0f) {
        return;
    }

    const float speed = kAnalogScrollBaseSpeed * GAMEPAD_ANALOG_SCROLL_SPEED;
    const float move_y = dy * speed;
    const float move_x = dx * speed;

    QMetaObject::invokeMethod(
        target,
        [target, move_x, move_y]() {
            if (!target || !target->main_document_view_has_document()) {
                return;
            }
            const bool truncated = target->move_document(move_x, move_y);
            target->maybe_open_adjacent_document_after_boundary_scroll(move_y, truncated);
            target->validate_render();
        },
        Qt::QueuedConnection
    );
}
