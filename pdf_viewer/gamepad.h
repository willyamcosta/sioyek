#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <algorithm>
#include <qobject.h>
#include <qtimer.h>

class MainWidget;

enum SioyekGamepadKey {
    Key_Gamepad_Base = 0x01200000,
    Key_Gamepad_A = 0x01200001,
    Key_Gamepad_B,
    Key_Gamepad_X,
    Key_Gamepad_Y,
    Key_Gamepad_LB,
    Key_Gamepad_RB,
    Key_Gamepad_LT,
    Key_Gamepad_RT,
    Key_Gamepad_Select,
    Key_Gamepad_Start,
    Key_Gamepad_Guide,
    Key_Gamepad_L3,
    Key_Gamepad_R3,
    Key_Gamepad_DpadUp,
    Key_Gamepad_DpadDown,
    Key_Gamepad_DpadLeft,
    Key_Gamepad_DpadRight,
    Key_Gamepad_LS_Up,
    Key_Gamepad_LS_Down,
    Key_Gamepad_LS_Left,
    Key_Gamepad_LS_Right,
    Key_Gamepad_RS_Up,
    Key_Gamepad_RS_Down,
    Key_Gamepad_RS_Left,
    Key_Gamepad_RS_Right,
    Key_Gamepad_Max
};

class GamepadManager : public QObject {
    Q_OBJECT

private:
    std::function<MainWidget*()> get_active_window_fn;
    std::atomic<bool> should_quit{false};
    std::thread reader_thread;

    struct GamepadDevice {
        int fd = -1;
        std::string path;
        std::string name;
    };

    std::vector<GamepadDevice> open_devices;
    std::string primary_device_name;
    std::atomic<bool> is_connected{false};

    // Analog stick and trigger states normalized to [-1.0, 1.0] (triggers: [0.0, 1.0])
    std::atomic<float> axis_left_x{0.0f};
    std::atomic<float> axis_left_y{0.0f};
    std::atomic<float> axis_right_x{0.0f};
    std::atomic<float> axis_right_y{0.0f};
    std::atomic<float> axis_lt{0.0f};
    std::atomic<float> axis_rt{0.0f};

    // Threshold state tracking for discrete button triggers
    bool lt_pressed = false;
    bool rt_pressed = false;
    bool dpad_left_pressed = false;
    bool dpad_right_pressed = false;
    bool dpad_up_pressed = false;
    bool dpad_down_pressed = false;

    bool ls_up_pressed = false;
    bool ls_down_pressed = false;
    bool ls_left_pressed = false;
    bool ls_right_pressed = false;

    bool rs_up_pressed = false;
    bool rs_down_pressed = false;
    bool rs_left_pressed = false;
    bool rs_right_pressed = false;

    QTimer analog_timer;

    void reader_loop();
    void scan_for_devices();
    void close_all_devices();
    void process_button_event(int button_index, bool pressed);
    void process_axis_event(int axis_index, int value);
    void dispatch_key(int key_code, bool pressed);

private slots:
    void on_analog_tick();

public:
    GamepadManager(std::function<MainWidget*()> active_window_getter, QObject* parent = nullptr);
    ~GamepadManager();

    bool is_device_connected() const { return is_connected.load(); }
    std::string get_device_name() const;
};
