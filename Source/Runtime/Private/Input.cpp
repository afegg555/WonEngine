#include "Input.h"
#include "Platform.h"
#include "Timer.h"
#include "MathUtils.h"

using namespace won::math;

namespace won::io
{
#ifdef _WIN32
#define KEY_DOWN(vk_code) (GetAsyncKeyState(vk_code) < 0)
#define KEY_TOGGLE(vk_code) ((GetAsyncKeyState(vk_code) & 1) != 0)
#else
#define KEY_DOWN(vk_code) 0
#define KEY_TOGGLE(vk_code) 0
#endif // WIN32

	won::platform::WindowType window;
	KeyboardState keyboard;
	MouseState mouse;
	bool double_click = false;
	double double_click_interval = 0.5;
	utils::Timer doubleclick_timer;
	float2 doubleclick_prevpos = float2(0, 0);

	void Input::Update(WindowType _window)
	{
		window = _window;

#ifdef _WIN32
		POINT p;
		GetCursorPos(&p);
		ScreenToClient(window, &p);
		mouse.position.x = (float)p.x;
		mouse.position.y = (float)p.y;
#endif
		mouse.left_button_press = KEY_DOWN(VK_LBUTTON);
		mouse.middle_button_press = KEY_DOWN(VK_MBUTTON);
		mouse.right_button_press = KEY_DOWN(VK_RBUTTON);

		double_click = false;
		if (IsPressed(MOUSE_BUTTON_LEFT))
		{
			XMFLOAT2 pos = mouse.position;
			double elapsed = doubleclick_timer.ElapsedSeconds();
			if (elapsed < double_click_interval && math::Distance(doubleclick_prevpos, pos) < 5)
			{
				double_click = true;
			}
			doubleclick_prevpos = pos;
		}
	}
	bool Input::IsDown(Button button)
	{
		uint16_t keycode = (uint16_t)button;

		switch (button)
		{
		case MOUSE_BUTTON_LEFT:
			if (mouse.left_button_press)
				return true;
			return false;
		case MOUSE_BUTTON_RIGHT:
			if (mouse.right_button_press)
				return true;
			return false;
		case MOUSE_BUTTON_MIDDLE:
			if (mouse.middle_button_press)
				return true;
			return false;
#ifdef _WIN32
		case KEYBOARD_BUTTON_UP:
			keycode = VK_UP;
			break;
		case KEYBOARD_BUTTON_DOWN:
			keycode = VK_DOWN;
			break;
		case KEYBOARD_BUTTON_LEFT:
			keycode = VK_LEFT;
			break;
		case KEYBOARD_BUTTON_RIGHT:
			keycode = VK_RIGHT;
			break;
		case KEYBOARD_BUTTON_SPACE:
			keycode = VK_SPACE;
			break;
		case KEYBOARD_BUTTON_RSHIFT:
			keycode = VK_RSHIFT;
			break;
		case KEYBOARD_BUTTON_LSHIFT:
			keycode = VK_LSHIFT;
			break;
		case KEYBOARD_BUTTON_F1:
			keycode = VK_F1;
			break;
		case KEYBOARD_BUTTON_F2:
			keycode = VK_F2;
			break;
		case KEYBOARD_BUTTON_F3:
			keycode = VK_F3;
			break;
		case KEYBOARD_BUTTON_F4:
			keycode = VK_F4;
			break;
		case KEYBOARD_BUTTON_F5:
			keycode = VK_F5;
			break;
		case KEYBOARD_BUTTON_F6:
			keycode = VK_F6;
			break;
		case KEYBOARD_BUTTON_F7:
			keycode = VK_F7;
			break;
		case KEYBOARD_BUTTON_F8:
			keycode = VK_F8;
			break;
		case KEYBOARD_BUTTON_F9:
			keycode = VK_F9;
			break;
		case KEYBOARD_BUTTON_F10:
			keycode = VK_F10;
			break;
		case KEYBOARD_BUTTON_F11:
			keycode = VK_F11;
			break;
		case KEYBOARD_BUTTON_F12:
			keycode = VK_F12;
			break;
		case KEYBOARD_BUTTON_ENTER:
			keycode = VK_RETURN;
			break;
		case KEYBOARD_BUTTON_ESCAPE:
			keycode = VK_ESCAPE;
			break;
		case KEYBOARD_BUTTON_HOME:
			keycode = VK_HOME;
			break;
		case KEYBOARD_BUTTON_LCONTROL:
			keycode = VK_LCONTROL;
			break;
		case KEYBOARD_BUTTON_RCONTROL:
			keycode = VK_RCONTROL;
			break;
		case KEYBOARD_BUTTON_INSERT:
			keycode = VK_INSERT;
			break;
		case KEYBOARD_BUTTON_DELETE:
			keycode = VK_DELETE;
			break;
		case KEYBOARD_BUTTON_BACKSPACE:
			keycode = VK_BACK;
			break;
		case KEYBOARD_BUTTON_PAGEDOWN:
			keycode = VK_NEXT;
			break;
		case KEYBOARD_BUTTON_PAGEUP:
			keycode = VK_PRIOR;
			break;
		case KEYBOARD_BUTTON_NUMPAD0:
			keycode = VK_NUMPAD0;
			break;
		case KEYBOARD_BUTTON_NUMPAD1:
			keycode = VK_NUMPAD1;
			break;
		case KEYBOARD_BUTTON_NUMPAD2:
			keycode = VK_NUMPAD2;
			break;
		case KEYBOARD_BUTTON_NUMPAD3:
			keycode = VK_NUMPAD3;
			break;
		case KEYBOARD_BUTTON_NUMPAD4:
			keycode = VK_NUMPAD4;
			break;
		case KEYBOARD_BUTTON_NUMPAD5:
			keycode = VK_NUMPAD5;
			break;
		case KEYBOARD_BUTTON_NUMPAD6:
			keycode = VK_NUMPAD6;
			break;
		case KEYBOARD_BUTTON_NUMPAD7:
			keycode = VK_NUMPAD7;
			break;
		case KEYBOARD_BUTTON_NUMPAD8:
			keycode = VK_NUMPAD8;
			break;
		case KEYBOARD_BUTTON_NUMPAD9:
			keycode = VK_NUMPAD9;
			break;
		case KEYBOARD_BUTTON_MULTIPLY:
			keycode = VK_MULTIPLY;
			break;
		case KEYBOARD_BUTTON_ADD:
			keycode = VK_ADD;
			break;
		case KEYBOARD_BUTTON_SEPARATOR:
			keycode = VK_SEPARATOR;
			break;
		case KEYBOARD_BUTTON_SUBTRACT:
			keycode = VK_SUBTRACT;
			break;
		case KEYBOARD_BUTTON_DECIMAL:
			keycode = VK_DECIMAL;
			break;
		case KEYBOARD_BUTTON_DIVIDE:
			keycode = VK_DIVIDE;
			break;
		case KEYBOARD_BUTTON_TAB:
			keycode = VK_TAB;
			break;
		case KEYBOARD_BUTTON_TILDE:
			keycode = VK_OEM_FJ_JISHO; // http://kbdedit.com/manual/low_level_vk_list.html
			break;
		case KEYBOARD_BUTTON_ALT:
			keycode = VK_LMENU;
			break;
		case KEYBOARD_BUTTON_ALTGR:
			keycode = VK_RMENU;
			break;
#endif // _WIN32
		default: break;
		}
#if defined(_WIN32)
		return KEY_DOWN(keycode) || KEY_TOGGLE(keycode);
#else
		return false;
#endif
		
	}

	UnorderedMap<Button, int> inputs;
	bool Input::IsPressed(Button button)
	{
		if (!IsDown(button))
			return false;

		auto iter = inputs.find(button);
		if (iter == inputs.end())
		{
			inputs.insert(std::make_pair(button, 0));
			return true;
		}

		return false;
	}
	bool Input::IsReleased(Button button)
	{
		auto iter = inputs.find(button);
		if (iter == inputs.end())
		{
			if (IsDown(button))
				inputs.insert(std::make_pair(button, 0));

			return false;
		}

		inputs.erase(iter);
		return true;
	}
	bool Input::IsDoubleClicked()
	{
		return double_click;
	}
	void Input::SetDoubleClickInterval(double seconds)
	{
		double_click_interval = seconds;
	}
	const KeyboardState& Input::GetKeyboardState()
	{
		return keyboard;
	}
	const MouseState& Input::GetMouseState()
	{
		return mouse;
	}
}