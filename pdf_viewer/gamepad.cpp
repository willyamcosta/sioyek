#include "gamepad.h"
#include "main_widget.h"
#include <iostream>
#include <cmath>
#include <qapplication.h>
#include <qcoreevent.h>
#include <qevent.h>
#include <qdebug.h>

#ifdef __linux__
#include <linux/joystick.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <errno.h>
#endif

extern bool GAMEPAD_ENABLED;
extern float GAMEPAD_DEADZONE;
extern float GAMEPAD_ANALOG_SCROLL_SPEED;

GamepadManager::GamepadManager(std::function<MainWidget*()> active_window_getter, QObject* parent)
    : QObject(parent), get_active_window_fn(std::move(active_window_getter)) {

    connect(&analog_timer, &QTimer::timeout, this, &GamepadManager::on_analog_tick);
    analog_timer.start(16); // ~60 Hz

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
    // Scan /dev/input/js0 through js15
    for (int i = 0; i < 16; i++) {
        std::string candidate = "/dev/input/js" + std::to_string(i);

        bool already_open = false;
        for (const auto& dev : open_devices) {
            if (dev.path == candidate) {
                already_open = true;
                break;
            }
        }
        if (already_open) continue;

        int fd = open(candidate.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            char name_buf[128] = { 0 };
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

            qInfo().noquote() << "Gamepad connected:" << QString::fromStdString(dev.name) << "(" + QString::fromStdString(dev.path) + ")";
        }
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
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_scan_time).count() >= 1000) {
            scan_for_devices();
            last_scan_time = now;
        }

        if (open_devices.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        std::vector<struct pollfd> pfds(open_devices.size());
        for (size_t i = 0; i < open_devices.size(); i++) {
            pfds[i].fd = open_devices[i].fd;
            pfds[i].events = POLLIN;
            pfds[i].revents = 0;
        }

        int ret = poll(pfds.data(), pfds.size(), 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (ret == 0) {
            continue; // timeout
        }

        std::vector<int> to_remove;
        for (size_t i = 0; i < pfds.size(); i++) {
            if (pfds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                qInfo().noquote() << "Gamepad disconnected:" << QString::fromStdString(open_devices[i].name);
                close(open_devices[i].fd);
                to_remove.push_back(i);
                continue;
            }

            if (pfds[i].revents & POLLIN) {
                struct js_event ev;
                bool read_error = false;
                while (true) {
                    ssize_t bytes = read(open_devices[i].fd, &ev, sizeof(ev));
                    if (bytes == sizeof(ev)) {
                        // Strip the synthetic initialization flag
                        int type = ev.type & ~JS_EVENT_INIT;

                        if (type == JS_EVENT_BUTTON) {
                            process_button_event(ev.number, ev.value != 0);
                        }
                        else if (type == JS_EVENT_AXIS) {
                            process_axis_event(ev.number, ev.value);
                        }
                    }
                    else {
                        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                            break; // drained current events
                        }
                        else {
                            read_error = true;
                            break;
                        }
                    }
                }

                if (read_error) {
                    qInfo().noquote() << "Gamepad disconnected or read error:" << QString::fromStdString(open_devices[i].name);
                    close(open_devices[i].fd);
                    to_remove.push_back(i);
                }
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
    int key = 0;
    switch (button_index) {
        case 0: key = Key_Gamepad_A; break;
        case 1: key = Key_Gamepad_B; break;
        case 2: key = Key_Gamepad_X; break;
        case 3: key = Key_Gamepad_Y; break;
        case 4: key = Key_Gamepad_LB; break;
        case 5: key = Key_Gamepad_RB; break;
        case 6: key = Key_Gamepad_Select; break;
        case 7: key = Key_Gamepad_Start; break;
        case 8: key = Key_Gamepad_Guide; break;
        case 9: key = Key_Gamepad_L3; break;
        case 10: key = Key_Gamepad_R3; break;
        // D-pad button fallback (supported by some controller drivers)
        case 11: key = Key_Gamepad_DpadUp; break;
        case 12: key = Key_Gamepad_DpadRight; break;
        case 13: key = Key_Gamepad_DpadDown; break;
        case 14: key = Key_Gamepad_DpadLeft; break;
        default: break;
    }

    if (key != 0) {
        dispatch_key(key, pressed);
    }
}

void GamepadManager::process_axis_event(int axis_index, int value) {
    float norm = std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);

    switch (axis_index) {
        case 0: // Left Stick X
            axis_left_x.store(norm);
            if (norm < -0.5f && !ls_left_pressed) { ls_left_pressed = true; dispatch_key(Key_Gamepad_LS_Left, true); }
            else if (norm > -0.25f && ls_left_pressed) { ls_left_pressed = false; dispatch_key(Key_Gamepad_LS_Left, false); }
            if (norm > 0.5f && !ls_right_pressed) { ls_right_pressed = true; dispatch_key(Key_Gamepad_LS_Right, true); }
            else if (norm < 0.25f && ls_right_pressed) { ls_right_pressed = false; dispatch_key(Key_Gamepad_LS_Right, false); }
            break;

        case 1: // Left Stick Y
            axis_left_y.store(norm);
            if (norm < -0.5f && !ls_up_pressed) { ls_up_pressed = true; dispatch_key(Key_Gamepad_LS_Up, true); }
            else if (norm > -0.25f && ls_up_pressed) { ls_up_pressed = false; dispatch_key(Key_Gamepad_LS_Up, false); }
            if (norm > 0.5f && !ls_down_pressed) { ls_down_pressed = true; dispatch_key(Key_Gamepad_LS_Down, true); }
            else if (norm < 0.25f && ls_down_pressed) { ls_down_pressed = false; dispatch_key(Key_Gamepad_LS_Down, false); }
            break;

        case 2: // Left Trigger (LT)
            axis_lt.store((norm + 1.0f) * 0.5f);
            if (value > 8000 && !lt_pressed) { lt_pressed = true; dispatch_key(Key_Gamepad_LT, true); }
            else if (value < 4000 && lt_pressed) { lt_pressed = false; dispatch_key(Key_Gamepad_LT, false); }
            break;

        case 3: // Right Stick X
            axis_right_x.store(norm);
            if (norm < -0.5f && !rs_left_pressed) { rs_left_pressed = true; dispatch_key(Key_Gamepad_RS_Left, true); }
            else if (norm > -0.25f && rs_left_pressed) { rs_left_pressed = false; dispatch_key(Key_Gamepad_RS_Left, false); }
            if (norm > 0.5f && !rs_right_pressed) { rs_right_pressed = true; dispatch_key(Key_Gamepad_RS_Right, true); }
            else if (norm < 0.25f && rs_right_pressed) { rs_right_pressed = false; dispatch_key(Key_Gamepad_RS_Right, false); }
            break;

        case 4: // Right Stick Y
            axis_right_y.store(norm);
            if (norm < -0.5f && !rs_up_pressed) { rs_up_pressed = true; dispatch_key(Key_Gamepad_RS_Up, true); }
            else if (norm > -0.25f && rs_up_pressed) { rs_up_pressed = false; dispatch_key(Key_Gamepad_RS_Up, false); }
            if (norm > 0.5f && !rs_down_pressed) { rs_down_pressed = true; dispatch_key(Key_Gamepad_RS_Down, true); }
            else if (norm < 0.25f && rs_down_pressed) { rs_down_pressed = false; dispatch_key(Key_Gamepad_RS_Down, false); }
            break;

        case 5: // Right Trigger (RT)
            axis_rt.store((norm + 1.0f) * 0.5f);
            if (value > 8000 && !rt_pressed) { rt_pressed = true; dispatch_key(Key_Gamepad_RT, true); }
            else if (value < 4000 && rt_pressed) { rt_pressed = false; dispatch_key(Key_Gamepad_RT, false); }
            break;

        case 6: // D-pad X (Hat0X)
            if (value < -16000 && !dpad_left_pressed) { dpad_left_pressed = true; dispatch_key(Key_Gamepad_DpadLeft, true); }
            else if (value >= -8000 && dpad_left_pressed) { dpad_left_pressed = false; dispatch_key(Key_Gamepad_DpadLeft, false); }
            if (value > 16000 && !dpad_right_pressed) { dpad_right_pressed = true; dispatch_key(Key_Gamepad_DpadRight, true); }
            else if (value <= 8000 && dpad_right_pressed) { dpad_right_pressed = false; dispatch_key(Key_Gamepad_DpadRight, false); }
            break;

        case 7: // D-pad Y (Hat0Y)
            if (value < -16000 && !dpad_up_pressed) { dpad_up_pressed = true; dispatch_key(Key_Gamepad_DpadUp, true); }
            else if (value >= -8000 && dpad_up_pressed) { dpad_up_pressed = false; dispatch_key(Key_Gamepad_DpadUp, false); }
            if (value > 16000 && !dpad_down_pressed) { dpad_down_pressed = true; dispatch_key(Key_Gamepad_DpadDown, true); }
            else if (value <= 8000 && dpad_down_pressed) { dpad_down_pressed = false; dispatch_key(Key_Gamepad_DpadDown, false); }
            break;

        default:
            break;
    }
}

void GamepadManager::dispatch_key(int key_code, bool pressed) {
    if (!GAMEPAD_ENABLED) return;

    QMetaObject::invokeMethod(this, [this, key_code, pressed]() {
        MainWidget* target = get_active_window_fn ? get_active_window_fn() : nullptr;
        if (!target) return;

        // If a modal or menu widget is active, map D-pad and face buttons to menu navigation
        if (target->has_active_menu_widget()) {
            int nav_key = 0;
            if (key_code == Key_Gamepad_DpadDown) nav_key = Qt::Key_Down;
            else if (key_code == Key_Gamepad_DpadUp) nav_key = Qt::Key_Up;
            else if (key_code == Key_Gamepad_DpadLeft) nav_key = Qt::Key_Left;
            else if (key_code == Key_Gamepad_DpadRight) nav_key = Qt::Key_Right;
            else if (key_code == Key_Gamepad_A) nav_key = Qt::Key_Return;
            else if (key_code == Key_Gamepad_B) nav_key = Qt::Key_Escape;

            if (nav_key != 0) {
                QWidget* focus_w = QApplication::focusWidget();
                if (!focus_w) focus_w = target;
                QKeyEvent* ev = new QKeyEvent(pressed ? QEvent::KeyPress : QEvent::KeyRelease, nav_key, Qt::NoModifier);
                QCoreApplication::postEvent(focus_w, ev);
                return;
            }
        }

        QKeyEvent* ev = new QKeyEvent(pressed ? QEvent::KeyPress : QEvent::KeyRelease, key_code, Qt::NoModifier);
        QCoreApplication::postEvent(target, ev);
    }, Qt::QueuedConnection);
}

void GamepadManager::on_analog_tick() {
    if (!GAMEPAD_ENABLED) return;

    MainWidget* target = get_active_window_fn ? get_active_window_fn() : nullptr;
    if (!target || !target->main_document_view_has_document()) return;
    if (target->has_active_menu_widget()) return;

    float sy = std::abs(axis_right_y.load()) > std::abs(axis_left_y.load()) ? axis_right_y.load() : axis_left_y.load();
    float sx = std::abs(axis_right_x.load()) > std::abs(axis_left_x.load()) ? axis_right_x.load() : axis_left_x.load();

    float dz = std::clamp(GAMEPAD_DEADZONE, 0.01f, 0.9f);

    float norm_y = 0.0f;
    if (std::abs(sy) > dz) {
        norm_y = std::copysign((std::abs(sy) - dz) / (1.0f - dz), sy);
    }

    float norm_x = 0.0f;
    if (std::abs(sx) > dz) {
        norm_x = std::copysign((std::abs(sx) - dz) / (1.0f - dz), sx);
    }

    if (norm_y != 0.0f || norm_x != 0.0f) {
        float speed = 18.0f * GAMEPAD_ANALOG_SCROLL_SPEED;
        float dy = norm_y * speed;
        float dx = norm_x * speed;

        bool truncated = target->move_document(dx, dy);
        target->maybe_open_adjacent_document_after_boundary_scroll(dy, truncated);
        target->validate_render();
    }
}
