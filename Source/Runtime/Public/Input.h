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

		GAMEPAD_BUTTON_A = 384,
		GAMEPAD_BUTTON_B,
		GAMEPAD_BUTTON_X,
		GAMEPAD_BUTTON_Y,
		GAMEPAD_BUTTON_BACK,
		GAMEPAD_BUTTON_START,
		GAMEPAD_BUTTON_DPAD_UP,
		GAMEPAD_BUTTON_DPAD_DOWN,
		GAMEPAD_BUTTON_DPAD_LEFT,
		GAMEPAD_BUTTON_DPAD_RIGHT,
		GAMEPAD_BUTTON_LEFT_SHOULDER,
		GAMEPAD_BUTTON_RIGHT_SHOULDER,
		GAMEPAD_BUTTON_LEFT_THUMB,
		GAMEPAD_BUTTON_RIGHT_THUMB,

		BUTTON_COUNT
    };

	enum class InputEventType
	{
		Button,
		MouseMove,
		MouseWheel,
		GamepadAxis,
		FocusLost,
		Character
	};

	// gamepad axes
	enum class InputAxis
	{
		None,
		GamepadLeftStickX,
		GamepadLeftStickY,
		GamepadRightStickX,
		GamepadRightStickY,
		GamepadLeftTrigger,
		GamepadRightTrigger
	};

	// represents raw input events from the platform
	struct InputEvent
	{
		InputEventType type = InputEventType::Button;
		uint32 device_index = 0;
		Button button = BUTTON_NONE;
		InputAxis axis = InputAxis::None;
		float2 position = float2(0, 0);
		float2 delta = float2(0, 0);
		float value = 0.0f;
		bool pressed = false;
		uint32 character = 0;
	};

	struct KeyboardState
	{
		bool digits[10] = {};
		bool characters[26] = {};
		bool buttons[KEYBOARD_BUTTON_ALTGR - KEYBOARD_BUTTON_UP + 1] = {};
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

	struct GamepadState
	{
		bool connected = false;
		bool buttons[GAMEPAD_BUTTON_RIGHT_THUMB - GAMEPAD_BUTTON_A + 1] = {};
		float2 left_stick = float2(0, 0);
		float2 right_stick = float2(0, 0);
		float left_trigger = 0.0f;
		float right_trigger = 0.0f;
	};

	enum class InputActionType
	{
		Button,
		Axis1D,
		Axis2D
	};

	struct InputActionState
	{
		InputActionType type = InputActionType::Button;
		bool down = false;
		bool pressed = false;
		bool released = false;
		float value = 0.0f;
		float2 axis = float2(0, 0);
	};

	WONENGINE_API void PushInputEvent(const InputEvent& event);
	WONENGINE_API void Update(WindowType window);
	WONENGINE_API void Reset();
	WONENGINE_API bool LoadActionMap(const String& path);
	WONENGINE_API void ClearActionMap();
	WONENGINE_API void SetMouseCaptured(bool captured);
	WONENGINE_API bool IsDown(Button button);
	WONENGINE_API bool IsPressed(Button button);
	WONENGINE_API bool IsReleased(Button button);
	WONENGINE_API bool IsDoubleClicked();
	WONENGINE_API void SetDoubleClickInterval(double seconds); // default is 0.5
	WONENGINE_API Button GetButtonFromString(StringView value);
	WONENGINE_API const InputActionState* GetActionState(StringView action_name);
	WONENGINE_API bool IsActionDown(StringView action_name);
	WONENGINE_API bool IsActionPressed(StringView action_name);
	WONENGINE_API bool IsActionReleased(StringView action_name);
	WONENGINE_API float GetActionValue(StringView action_name);
	WONENGINE_API float2 GetActionAxis2D(StringView action_name);

	WONENGINE_API const KeyboardState& GetKeyboardState();
	WONENGINE_API const MouseState& GetMouseState();
	WONENGINE_API const GamepadState* GetGamepadState(uint32 device_index = 0);

	// Characters typed this frame (from platform text input), valid until the next Update.
	WONENGINE_API const String& GetTextInput();
	// While suppressed, game-facing queries (IsDown/IsPressed/IsReleased and action states) report neutral,
	// so an overlay that captures input can stop the game from also receiving it. Reset each Update.
	WONENGINE_API void SetInputSuppressed(bool suppressed);
	WONENGINE_API bool IsInputSuppressed();
}
