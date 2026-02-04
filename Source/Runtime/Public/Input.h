#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "Platform.h"

#include <memory>

using namespace won::platform;

namespace won::io
{
    enum Button
    {
        BUTTON_NONE = 0,

		MOUSE_BUTTON_LEFT,
		MOUSE_BUTTON_RIGHT,
		MOUSE_BUTTON_MIDDLE,

        DIGIT_RANGE_START = 48, // digit 0

        CHARACTER_RANGE_START = 65, // letter A

		KEYBOARD_BUTTON_UP = 256,
		KEYBOARD_BUTTON_DOWN,
		KEYBOARD_BUTTON_LEFT,
		KEYBOARD_BUTTON_RIGHT,
		KEYBOARD_BUTTON_SPACE,
		KEYBOARD_BUTTON_RSHIFT,
		KEYBOARD_BUTTON_LSHIFT,
		KEYBOARD_BUTTON_F1,
		KEYBOARD_BUTTON_F2,
		KEYBOARD_BUTTON_F3,
		KEYBOARD_BUTTON_F4,
		KEYBOARD_BUTTON_F5,
		KEYBOARD_BUTTON_F6,
		KEYBOARD_BUTTON_F7,
		KEYBOARD_BUTTON_F8,
		KEYBOARD_BUTTON_F9,
		KEYBOARD_BUTTON_F10,
		KEYBOARD_BUTTON_F11,
		KEYBOARD_BUTTON_F12,
		KEYBOARD_BUTTON_ENTER,
		KEYBOARD_BUTTON_ESCAPE,
		KEYBOARD_BUTTON_HOME,
		KEYBOARD_BUTTON_RCONTROL,
		KEYBOARD_BUTTON_LCONTROL,
		KEYBOARD_BUTTON_DELETE,
		KEYBOARD_BUTTON_BACKSPACE,
		KEYBOARD_BUTTON_PAGEDOWN,
		KEYBOARD_BUTTON_PAGEUP,
		KEYBOARD_BUTTON_NUMPAD0,
		KEYBOARD_BUTTON_NUMPAD1,
		KEYBOARD_BUTTON_NUMPAD2,
		KEYBOARD_BUTTON_NUMPAD3,
		KEYBOARD_BUTTON_NUMPAD4,
		KEYBOARD_BUTTON_NUMPAD5,
		KEYBOARD_BUTTON_NUMPAD6,
		KEYBOARD_BUTTON_NUMPAD7,
		KEYBOARD_BUTTON_NUMPAD8,
		KEYBOARD_BUTTON_NUMPAD9,
		KEYBOARD_BUTTON_MULTIPLY,
		KEYBOARD_BUTTON_ADD,
		KEYBOARD_BUTTON_SEPARATOR,
		KEYBOARD_BUTTON_SUBTRACT,
		KEYBOARD_BUTTON_DECIMAL,
		KEYBOARD_BUTTON_DIVIDE,
		KEYBOARD_BUTTON_TAB,
		KEYBOARD_BUTTON_TILDE,
		KEYBOARD_BUTTON_INSERT,
		KEYBOARD_BUTTON_ALT,
		KEYBOARD_BUTTON_ALTGR,

		BUTTON_COUNT
    };

	struct KeyboardState
	{
		bool buttons[Button::BUTTON_COUNT] = {};
	};

	struct MouseState
	{
		float2 position = float2(0, 0);
		float2 delta_position = float2(0, 0);
		float delta_wheel = 0;
		float pressure = 1.0f;
		bool left_button_press = false;
		bool middle_button_press = false;
		bool right_button_press = false;
	};

    class WONENGINE_API Input
    {
    public:
        static void Update(WindowType window);
        static bool IsDown(Button button);
        static bool IsPressed(Button button);
        static bool IsReleased(Button button);
		static bool IsDoubleClicked();
		static void SetDoubleClickInterval(double seconds); // default is 0.5

		static const KeyboardState& GetKeyboardState();
		static const MouseState& GetMouseState();
    };
}
