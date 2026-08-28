#include "Input.h"
#include "InputActionMap.h"
#include "FileSystem.h"
#include "JsonArchive.h"
#include "Platform.h"
#include "Timer.h"
#include "MathUtils.h"
#include "StringUtils.h"

#if defined(_WIN32)
#include <xinput.h>
#endif

using namespace won::math;

namespace won::io
{
    namespace action_map_format = input_action_map_format;

    namespace
    {
        constexpr uint32 max_gamepad_count = 4;
        constexpr float action_deadzone = 0.001f; // means that any value within the range [-deadzone, deadzone] is considered as zero

        enum class InputBindingSource
        {
            Button,
            Axis1D,
            Axis2D
        };

        enum class InputBindingAxis
        {
            None,
            X,
            Y
        };

        struct InputActionBinding
        {
            String action_name;
            InputBindingSource source = InputBindingSource::Button;
            Button button = BUTTON_NONE;
            InputAxis axis_x = InputAxis::None;
            InputAxis axis_y = InputAxis::None;
            InputBindingAxis target_axis = InputBindingAxis::None;
            float scale = 1.0f;
        };

        struct InputActionRuntimeState
        {
            InputActionState state;
            bool previous_down = false;
        };

        static won::platform::WindowType window = nullptr;
        static KeyboardState keyboard;
        static KeyboardState previous_keyboard;
        static MouseState mouse;
        static MouseState previous_mouse;
        static bool mouse_captured = false;
        static GamepadState gamepads[max_gamepad_count];
        static GamepadState previous_gamepads[max_gamepad_count];
        static Vector<InputEvent> input_events;
        static String text_input;
        static bool input_suppressed = false;
        static bool mouse_position_initialized = false;
        static bool double_click = false;
        static double double_click_interval = 0.5;
        static utils::Timer doubleclick_timer;
        static float2 doubleclick_prevpos = float2(0, 0);
        static bool input_active = false;
        static uint32 action_map_version = 1;
        static String action_map_name;
		static Vector<InputActionBinding> action_bindings; // string name of the action -> binding info (button/axis, scale, etc) !! multiple bindings can map to the same action
		static UnorderedMap<String, InputActionRuntimeState> action_states; // string name of the action -> runtime state (pressed/released, value, etc)

#if defined(_WIN32)
        using XInputGetStateFunc = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
        static bool xinput_loaded = false;
        static HMODULE xinput_library = nullptr;
        static XInputGetStateFunc xinput_get_state = nullptr;
#endif
    }

    void PushInputEvent(const InputEvent& event)
    {
        input_events.push_back(event);
    }

    void Reset()
    {
        input_active = false;
        keyboard = {};
        previous_keyboard = {};
        mouse = {};
        previous_mouse = {};
        for (uint32 gamepad_index = 0; gamepad_index < max_gamepad_count; ++gamepad_index)
        {
            gamepads[gamepad_index] = {};
            previous_gamepads[gamepad_index] = {};
        }
        input_events.clear();
        text_input.clear();
        input_suppressed = false;
        mouse_position_initialized = false;
        double_click = false;
        doubleclick_prevpos = float2(0, 0);
        for (auto& entry : action_states)
        {
            entry.second.previous_down = false;
            entry.second.state.down = false;
            entry.second.state.pressed = false;
            entry.second.state.released = false;
            entry.second.state.value = 0.0f;
            entry.second.state.axis = float2(0, 0);
        }
    }

    void Update(WindowType _window)
    {
        input_active = true;
        window = _window;
        previous_keyboard = keyboard;
        previous_mouse = mouse;
        for (uint32 gamepad_index = 0; gamepad_index < max_gamepad_count; ++gamepad_index)
        {
            previous_gamepads[gamepad_index] = gamepads[gamepad_index];
        }
        mouse.delta_position = float2(0, 0);
        mouse.delta_wheel = 0.0f;
        double_click = false;
        text_input.clear();
        input_suppressed = false;

#if defined(_WIN32)
		// handle XInput gamepad input
        if (!xinput_loaded)
        {
            xinput_loaded = true;
            const char* dll_names[] =
            {
                "xinput1_4.dll",
                "xinput1_3.dll",
                "xinput9_1_0.dll",
                "xinput1_2.dll",
                "xinput1_1.dll"
            };
            for (const char* dll_name : dll_names)
            {
                xinput_library = LoadLibraryA(dll_name);
                if (!xinput_library)
                {
                    continue;
                }

                xinput_get_state = reinterpret_cast<XInputGetStateFunc>(GetProcAddress(xinput_library, "XInputGetState"));
                if (xinput_get_state)
                {
                    break;
                }
                FreeLibrary(xinput_library);
                xinput_library = nullptr;
            }
        }

        if (xinput_get_state)
        {
            const struct
            {
                WORD mask;
                Button button;
            } button_map[] =
            {
                { XINPUT_GAMEPAD_A, GAMEPAD_BUTTON_A },
                { XINPUT_GAMEPAD_B, GAMEPAD_BUTTON_B },
                { XINPUT_GAMEPAD_X, GAMEPAD_BUTTON_X },
                { XINPUT_GAMEPAD_Y, GAMEPAD_BUTTON_Y },
                { XINPUT_GAMEPAD_BACK, GAMEPAD_BUTTON_BACK },
                { XINPUT_GAMEPAD_START, GAMEPAD_BUTTON_START },
                { XINPUT_GAMEPAD_DPAD_UP, GAMEPAD_BUTTON_DPAD_UP },
                { XINPUT_GAMEPAD_DPAD_DOWN, GAMEPAD_BUTTON_DPAD_DOWN },
                { XINPUT_GAMEPAD_DPAD_LEFT, GAMEPAD_BUTTON_DPAD_LEFT },
                { XINPUT_GAMEPAD_DPAD_RIGHT, GAMEPAD_BUTTON_DPAD_RIGHT },
                { XINPUT_GAMEPAD_LEFT_SHOULDER, GAMEPAD_BUTTON_LEFT_SHOULDER },
                { XINPUT_GAMEPAD_RIGHT_SHOULDER, GAMEPAD_BUTTON_RIGHT_SHOULDER },
                { XINPUT_GAMEPAD_LEFT_THUMB, GAMEPAD_BUTTON_LEFT_THUMB },
                { XINPUT_GAMEPAD_RIGHT_THUMB, GAMEPAD_BUTTON_RIGHT_THUMB }
            };
            const InputAxis axis_order[] =
            {
                InputAxis::GamepadLeftStickX,
                InputAxis::GamepadLeftStickY,
                InputAxis::GamepadRightStickX,
                InputAxis::GamepadRightStickY,
                InputAxis::GamepadLeftTrigger,
                InputAxis::GamepadRightTrigger
            };

            for (uint32 gamepad_index = 0; gamepad_index < max_gamepad_count; ++gamepad_index)
            {
                XINPUT_STATE native_state = {};
				if (xinput_get_state(gamepad_index, &native_state) != ERROR_SUCCESS) // if the controller is not connected or an error occurred
                {
                    const bool was_connected = gamepads[gamepad_index].connected;
                    if (was_connected)
                    {
						// generate button release events for all buttons that were previously pressed
                        for (const auto& button_entry : button_map)
                        {
                            const uint32 gamepad_button_index = static_cast<uint32>(button_entry.button) - static_cast<uint32>(GAMEPAD_BUTTON_A);
                            if (gamepads[gamepad_index].buttons[gamepad_button_index])
                            {
                                InputEvent event = {};
                                event.type = InputEventType::Button;
                                event.device_index = gamepad_index;
                                event.button = button_entry.button;
                                event.pressed = false;
                                input_events.push_back(event);
                            }
                        }
                        for (InputAxis axis : axis_order)
                        {
                            InputEvent event = {};
                            event.type = InputEventType::GamepadAxis;
                            event.device_index = gamepad_index;
                            event.axis = axis;
                            event.value = 0.0f;
                            input_events.push_back(event);
                        }
                    }
                    gamepads[gamepad_index].connected = false;
                    continue;
                }

                gamepads[gamepad_index].connected = true;
				// generate button events
                for (const auto& button_entry : button_map)
                {
                    const bool pressed = (native_state.Gamepad.wButtons & button_entry.mask) != 0;
                    const uint32 gamepad_button_index = static_cast<uint32>(button_entry.button) - static_cast<uint32>(GAMEPAD_BUTTON_A);
                    if (gamepads[gamepad_index].buttons[gamepad_button_index] != pressed)
                    {
                        InputEvent event = {};
                        event.type = InputEventType::Button;
                        event.device_index = gamepad_index;
                        event.button = button_entry.button;
                        event.pressed = pressed;
                        input_events.push_back(event);
                    }
                }

                const struct
                {
                    InputAxis axis;
                    SHORT value;
					SHORT deadzone; // means that any value within the range [-deadzone, deadzone] is considered as zero
                } stick_samples[] =
                {
                    { InputAxis::GamepadLeftStickX, native_state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE },
                    { InputAxis::GamepadLeftStickY, native_state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE },
                    { InputAxis::GamepadRightStickX, native_state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE },
                    { InputAxis::GamepadRightStickY, native_state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE }
                };
                for (const auto& sample : stick_samples)
                {
                    float value = 0.0f;
                    const int sign = sample.value < 0 ? -1 : 1;
                    const int abs_value = std::abs(static_cast<int>(sample.value));
                    if (abs_value > static_cast<int>(sample.deadzone))
                    {
                        value = std::clamp(static_cast<float>(abs_value - sample.deadzone) / static_cast<float>(32767 - sample.deadzone), 0.0f, 1.0f) * static_cast<float>(sign);
                    }
                    InputEvent event = {};
                    event.type = InputEventType::GamepadAxis;
                    event.device_index = gamepad_index;
                    event.axis = sample.axis;
                    event.value = value;
                    input_events.push_back(event);
                }

                const struct
                {
                    InputAxis axis;
                    BYTE value;
                } trigger_samples[] =
                {
                    { InputAxis::GamepadLeftTrigger, native_state.Gamepad.bLeftTrigger },
                    { InputAxis::GamepadRightTrigger, native_state.Gamepad.bRightTrigger }
                };
                for (const auto& sample : trigger_samples)
                {
                    float value = 0.0f;
                    if (sample.value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
                    {
                        value = std::clamp(static_cast<float>(sample.value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD), 0.0f, 1.0f);
                    }
                    InputEvent event = {};
                    event.type = InputEventType::GamepadAxis;
                    event.device_index = gamepad_index;
                    event.axis = sample.axis;
                    event.value = value;
                    input_events.push_back(event);
                }
            }
        }
#endif

        for (const InputEvent& event : input_events)
        {
            if (event.type == InputEventType::FocusLost)
            {
                input_active = false;
                keyboard = {};
                previous_keyboard = {};
                mouse = {};
                previous_mouse = {};
                for (uint32 gamepad_index = 0; gamepad_index < max_gamepad_count; ++gamepad_index)
                {
                    gamepads[gamepad_index] = {};
                    previous_gamepads[gamepad_index] = {};
                }
                mouse_position_initialized = false;
                double_click = false;
                doubleclick_prevpos = float2(0, 0);
                for (auto& entry : action_states)
                {
                    entry.second.previous_down = false;
                    entry.second.state.down = false;
                    entry.second.state.pressed = false;
                    entry.second.state.released = false;
                    entry.second.state.value = 0.0f;
                    entry.second.state.axis = float2(0, 0);
                }
                continue;
            }

            if (event.type == InputEventType::Character)
            {
                if (event.character < 0x80)
                {
                    text_input += static_cast<char>(event.character);
                }
                continue;
            }

            if (event.type == InputEventType::Button)
            {
                const uint32 button_index = static_cast<uint32>(event.button);
                if (button_index > static_cast<uint32>(BUTTON_NONE) && button_index < static_cast<uint32>(BUTTON_COUNT))
                {
                    if (mouse_captured && event.pressed && (event.button == MOUSE_BUTTON_LEFT || event.button == MOUSE_BUTTON_RIGHT || event.button == MOUSE_BUTTON_MIDDLE))
                    {
                        continue;
                    }
                    if (event.button == MOUSE_BUTTON_LEFT)
                    {
                        mouse.left_button_press = event.pressed;
                    }
                    else if (event.button == MOUSE_BUTTON_RIGHT)
                    {
                        mouse.right_button_press = event.pressed;
                    }
                    else if (event.button == MOUSE_BUTTON_MIDDLE)
                    {
                        mouse.middle_button_press = event.pressed;
                    }
                    else if (event.button >= GAMEPAD_BUTTON_A && event.button < BUTTON_COUNT)
                    {
                        if (event.device_index < max_gamepad_count)
                        {
                            const uint32 gamepad_button_index = button_index - static_cast<uint32>(GAMEPAD_BUTTON_A);
                            gamepads[event.device_index].buttons[gamepad_button_index] = event.pressed;
                        }
                    }
                    else
                    {
                        if (button_index >= static_cast<uint32>('0') && button_index <= static_cast<uint32>('9'))
                        {
                            keyboard.digits[button_index - static_cast<uint32>('0')] = event.pressed;
                        }
                        else if (button_index >= static_cast<uint32>('A') && button_index <= static_cast<uint32>('Z'))
                        {
                            keyboard.characters[button_index - static_cast<uint32>('A')] = event.pressed;
                        }
                        else if (button_index >= static_cast<uint32>(KEYBOARD_BUTTON_UP) && button_index <= static_cast<uint32>(KEYBOARD_BUTTON_ALTGR))
                        {
                            keyboard.buttons[button_index - static_cast<uint32>(KEYBOARD_BUTTON_UP)] = event.pressed;
                        }
                    }

                    if (event.button == MOUSE_BUTTON_LEFT || event.button == MOUSE_BUTTON_RIGHT || event.button == MOUSE_BUTTON_MIDDLE)
                    {
                        if (mouse_position_initialized)
                        {
                            mouse.delta_position.x += event.position.x - mouse.position.x;
                            mouse.delta_position.y += event.position.y - mouse.position.y;
                        }
                        mouse.position = event.position;
                        mouse_position_initialized = true;
                    }
                }
                continue;
            }

            if (event.type == InputEventType::MouseMove)
            {
                if (mouse_position_initialized)
                {
                    mouse.delta_position.x += event.position.x - mouse.position.x;
                    mouse.delta_position.y += event.position.y - mouse.position.y;
                }
                else
                {
                    mouse.delta_position.x += event.delta.x;
                    mouse.delta_position.y += event.delta.y;
                }
                mouse.position = event.position;
                mouse_position_initialized = true;
                continue;
            }

            if (event.type == InputEventType::MouseWheel)
            {
                mouse.delta_wheel += event.value;
                continue;
            }

            if (event.type == InputEventType::GamepadAxis && event.device_index < max_gamepad_count)
            {
                GamepadState& gamepad = gamepads[event.device_index];
                switch (event.axis)
                {
                case InputAxis::GamepadLeftStickX:
                    gamepad.left_stick.x = event.value;
                    break;
                case InputAxis::GamepadLeftStickY:
                    gamepad.left_stick.y = event.value;
                    break;
                case InputAxis::GamepadRightStickX:
                    gamepad.right_stick.x = event.value;
                    break;
                case InputAxis::GamepadRightStickY:
                    gamepad.right_stick.y = event.value;
                    break;
                case InputAxis::GamepadLeftTrigger:
                    gamepad.left_trigger = event.value;
                    break;
                case InputAxis::GamepadRightTrigger:
                    gamepad.right_trigger = event.value;
                    break;
                default:
                    break;
                }
            }
        }
        input_events.clear();

        if (IsPressed(MOUSE_BUTTON_LEFT))
        {
            XMFLOAT2 pos = mouse.position;
            const double elapsed = doubleclick_timer.ElapsedSeconds();
            if (elapsed < double_click_interval && math::Distance(doubleclick_prevpos, pos) < 5)
            {
                double_click = true;
            }
            doubleclick_prevpos = pos;
            doubleclick_timer.Reset();
        }

		// initialize action states for the current frame
        for (auto& entry : action_states)
        {
            entry.second.previous_down = entry.second.state.down;
            entry.second.state.down = false;
            entry.second.state.pressed = false;
            entry.second.state.released = false;
            entry.second.state.value = 0.0f;
            entry.second.state.axis = float2(0, 0);
        }

        if (!input_active)
        {
            for (auto& entry : action_states)
            {
                entry.second.state.released = entry.second.previous_down;
            }
            return;
        }

        for (const InputActionBinding& binding : action_bindings)
        {
            auto state_it = action_states.find(binding.action_name);
            if (state_it == action_states.end())
            {
                continue;
            }

            InputActionState& state = state_it->second.state;
            float source_value = 0.0f;
            float2 source_axis = float2(0, 0);
            bool source_active = false;
            if (binding.source == InputBindingSource::Button)
            {
                const uint32 button_index = static_cast<uint32>(binding.button);
                if (input_active)
                {
                    if (binding.button == MOUSE_BUTTON_LEFT)
                    {
                        source_active = mouse.left_button_press;
                    }
                    else if (binding.button == MOUSE_BUTTON_RIGHT)
                    {
                        source_active = mouse.right_button_press;
                    }
                    else if (binding.button == MOUSE_BUTTON_MIDDLE)
                    {
                        source_active = mouse.middle_button_press;
                    }
                    else if (button_index >= static_cast<uint32>('0') && button_index <= static_cast<uint32>('9'))
                    {
                        source_active = keyboard.digits[button_index - static_cast<uint32>('0')];
                    }
                    else if (button_index >= static_cast<uint32>('A') && button_index <= static_cast<uint32>('Z'))
                    {
                        source_active = keyboard.characters[button_index - static_cast<uint32>('A')];
                    }
                    else if (button_index >= static_cast<uint32>(KEYBOARD_BUTTON_UP) && button_index <= static_cast<uint32>(KEYBOARD_BUTTON_ALTGR))
                    {
                        source_active = keyboard.buttons[button_index - static_cast<uint32>(KEYBOARD_BUTTON_UP)];
                    }
                    else if (button_index >= static_cast<uint32>(GAMEPAD_BUTTON_A) && button_index <= static_cast<uint32>(GAMEPAD_BUTTON_RIGHT_THUMB))
                    {
                        const uint32 gamepad_button_index = button_index - static_cast<uint32>(GAMEPAD_BUTTON_A);
                        for (uint32 gamepad_index = 0; gamepad_index < max_gamepad_count; ++gamepad_index)
                        {
                            if (gamepads[gamepad_index].buttons[gamepad_button_index])
                            {
                                source_active = true;
                                break;
                            }
                        }
                    }
                }
                source_value = source_active ? binding.scale : 0.0f;
            }
            else if (binding.source == InputBindingSource::Axis1D || binding.source == InputBindingSource::Axis2D)
            {
                const InputAxis axes[] = { binding.axis_x, binding.axis_y };
                float values[] = { 0.0f, 0.0f };
                for (uint32 axis_index = 0; axis_index < 2; ++axis_index)
                {
                    switch (axes[axis_index])
                    {
                    case InputAxis::GamepadLeftStickX:
                        values[axis_index] = gamepads[0].left_stick.x;
                        break;
                    case InputAxis::GamepadLeftStickY:
                        values[axis_index] = gamepads[0].left_stick.y;
                        break;
                    case InputAxis::GamepadRightStickX:
                        values[axis_index] = gamepads[0].right_stick.x;
                        break;
                    case InputAxis::GamepadRightStickY:
                        values[axis_index] = gamepads[0].right_stick.y;
                        break;
                    case InputAxis::GamepadLeftTrigger:
                        values[axis_index] = gamepads[0].left_trigger;
                        break;
                    case InputAxis::GamepadRightTrigger:
                        values[axis_index] = gamepads[0].right_trigger;
                        break;
                    default:
                        break;
                    }
                }

                if (binding.source == InputBindingSource::Axis1D)
                {
                    source_value = values[0] * binding.scale;
                    source_active = std::fabs(source_value) > action_deadzone;
                }
                else
                {
                    source_axis.x = values[0] * binding.scale;
                    source_axis.y = values[1] * binding.scale;
                    source_active = std::fabs(source_axis.x) > action_deadzone || std::fabs(source_axis.y) > action_deadzone;
                }
            }

            if (state.type == InputActionType::Button)
            {
                if (source_active)
                {
                    state.down = true;
                    state.value = std::max(state.value, std::fabs(source_value) > action_deadzone ? source_value : 1.0f);
                }
            }
            else if (state.type == InputActionType::Axis1D)
            {
                state.value += binding.source == InputBindingSource::Axis2D ? source_axis.x : source_value;
            }
            else if (state.type == InputActionType::Axis2D)
            {
                if (binding.source == InputBindingSource::Axis2D)
                {
                    state.axis.x += source_axis.x;
                    state.axis.y += source_axis.y;
                }
                else if (binding.target_axis == InputBindingAxis::Y)
                {
                    state.axis.y += source_value;
                }
                else
                {
                    state.axis.x += source_value;
                }
            }
        }

        for (auto& entry : action_states)
        {
            InputActionState& state = entry.second.state;
            if (state.type == InputActionType::Axis1D)
            {
                state.value = std::clamp(state.value, -1.0f, 1.0f);
                state.down = std::fabs(state.value) > action_deadzone;
            }
            else if (state.type == InputActionType::Axis2D)
            {
                state.axis.x = std::clamp(state.axis.x, -1.0f, 1.0f);
                state.axis.y = std::clamp(state.axis.y, -1.0f, 1.0f);
                state.value = std::sqrt(state.axis.x * state.axis.x + state.axis.y * state.axis.y);
                state.down = state.value > action_deadzone;
            }
            state.pressed = state.down && !entry.second.previous_down;
            state.released = !state.down && entry.second.previous_down;
        }
    }

    void SetMouseCaptured(bool captured)
    {
        mouse_captured = captured;
    }

    bool LoadActionMap(const String& path)
    {
        ClearActionMap();
        if (path.empty() || !IsFile(path))
        {
            return false;
        }

        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromFile(path) || !archive.BeginObject())
        {
            return false;
        }

        archive.Field(action_map_format::field_version, action_map_version);
        archive.Field(action_map_format::field_name, action_map_name);

        if (archive.BeginArray(action_map_format::field_actions))
        {
            const Size action_count = archive.GetArraySize();
            for (Size action_index = 0; action_index < action_count; ++action_index)
            {
                if (!archive.BeginItem())
                {
                    break;
                }
                if (archive.BeginObject())
                {
                    String action_name;
                    String type_name = action_map_format::type_button;
                    archive.Field(action_map_format::field_name, action_name);
                    archive.Field(action_map_format::field_type, type_name);
                    if (!action_name.empty())
                    {
                        type_name = utils::ToUpper(type_name);
                        InputActionRuntimeState runtime_state = {};
                        if (type_name == action_map_format::type_axis1d || type_name == action_map_format::type_axis)
                        {
                            runtime_state.state.type = InputActionType::Axis1D;
                        }
                        else if (type_name == action_map_format::type_axis2d || type_name == action_map_format::type_vector2)
                        {
                            runtime_state.state.type = InputActionType::Axis2D;
                        }
                        else
                        {
                            runtime_state.state.type = InputActionType::Button;
                        }
                        action_states[action_name] = runtime_state;
                    }
                    archive.EndObject();
                }
                archive.EndItem();
            }
            archive.EndArray();
        }

        if (archive.BeginArray(action_map_format::field_bindings))
        {
            const Size binding_count = archive.GetArraySize();
            for (Size binding_index = 0; binding_index < binding_count; ++binding_index)
            {
                if (!archive.BeginItem())
                {
                    break;
                }
                if (archive.BeginObject())
                {
                    String action_name;
                    String path_value;
                    String axis_name;
                    float scale = 1.0f;
                    archive.Field(action_map_format::field_action, action_name);
                    archive.Field(action_map_format::field_path, path_value);
                    archive.Field(action_map_format::field_axis, axis_name);
                    archive.Field(action_map_format::field_scale, scale);

                    InputActionBinding binding = {};
                    binding.action_name = action_name;
                    binding.scale = scale;
                    axis_name = utils::ToUpper(axis_name);
                    if (axis_name == action_map_format::axis_x)
                    {
                        binding.target_axis = InputBindingAxis::X;
                    }
                    else if (axis_name == action_map_format::axis_y)
                    {
                        binding.target_axis = InputBindingAxis::Y;
                    }

                    bool parsed_binding = false;
                    const Size separator_pos = path_value.find('/');
                    String device;
                    String control = path_value;
                    if (separator_pos != String::npos)
                    {
                        device = path_value.substr(0, separator_pos);
                        control = path_value.substr(separator_pos + 1);
                        device = utils::ToUpper(device);
                    }

                    if (separator_pos == String::npos || device == action_map_format::device_keyboard)
                    {
                        binding.source = InputBindingSource::Button;
                        binding.button = GetButtonFromString(control);
                        parsed_binding = binding.button != BUTTON_NONE;
                    }
                    else if (device == action_map_format::device_mouse)
                    {
                        control = utils::ToUpper(control);
                        binding.source = InputBindingSource::Button;
                        if (control == action_map_format::mouse_left)
                        {
                            binding.button = MOUSE_BUTTON_LEFT;
                        }
                        else if (control == action_map_format::mouse_right)
                        {
                            binding.button = MOUSE_BUTTON_RIGHT;
                        }
                        else if (control == action_map_format::mouse_middle)
                        {
                            binding.button = MOUSE_BUTTON_MIDDLE;
                        }
                        parsed_binding = binding.button != BUTTON_NONE;
                    }
                    else if (device == action_map_format::device_gamepad)
                    {
                        control = utils::ToUpper(control);
                        if (control == action_map_format::gamepad_left_stick || control == action_map_format::gamepad_left_stick_alias)
                        {
                            binding.source = InputBindingSource::Axis2D;
                            binding.axis_x = InputAxis::GamepadLeftStickX;
                            binding.axis_y = InputAxis::GamepadLeftStickY;
                            parsed_binding = true;
                        }
                        else if (control == action_map_format::gamepad_right_stick || control == action_map_format::gamepad_right_stick_alias)
                        {
                            binding.source = InputBindingSource::Axis2D;
                            binding.axis_x = InputAxis::GamepadRightStickX;
                            binding.axis_y = InputAxis::GamepadRightStickY;
                            parsed_binding = true;
                        }
                        else if (control == action_map_format::gamepad_left_stick_x || control == action_map_format::gamepad_left_stick_x_alias)
                        {
                            binding.source = InputBindingSource::Axis1D;
                            binding.axis_x = InputAxis::GamepadLeftStickX;
                            parsed_binding = true;
                        }
                        else if (control == action_map_format::gamepad_left_stick_y || control == action_map_format::gamepad_left_stick_y_alias)
                        {
                            binding.source = InputBindingSource::Axis1D;
                            binding.axis_x = InputAxis::GamepadLeftStickY;
                            parsed_binding = true;
                        }
                        else if (control == action_map_format::gamepad_right_stick_x || control == action_map_format::gamepad_right_stick_x_alias)
                        {
                            binding.source = InputBindingSource::Axis1D;
                            binding.axis_x = InputAxis::GamepadRightStickX;
                            parsed_binding = true;
                        }
                        else if (control == action_map_format::gamepad_right_stick_y || control == action_map_format::gamepad_right_stick_y_alias)
                        {
                            binding.source = InputBindingSource::Axis1D;
                            binding.axis_x = InputAxis::GamepadRightStickY;
                            parsed_binding = true;
                        }
                        else if (control == action_map_format::gamepad_left_trigger || control == action_map_format::gamepad_left_trigger_alias || control == action_map_format::gamepad_left_trigger_short)
                        {
                            binding.source = InputBindingSource::Axis1D;
                            binding.axis_x = InputAxis::GamepadLeftTrigger;
                            parsed_binding = true;
                        }
                        else if (control == action_map_format::gamepad_right_trigger || control == action_map_format::gamepad_right_trigger_alias || control == action_map_format::gamepad_right_trigger_short)
                        {
                            binding.source = InputBindingSource::Axis1D;
                            binding.axis_x = InputAxis::GamepadRightTrigger;
                            parsed_binding = true;
                        }
                        else
                        {
                            binding.source = InputBindingSource::Button;
                            if (control == action_map_format::gamepad_a)
                            {
                                binding.button = GAMEPAD_BUTTON_A;
                            }
                            else if (control == action_map_format::gamepad_b)
                            {
                                binding.button = GAMEPAD_BUTTON_B;
                            }
                            else if (control == action_map_format::gamepad_x)
                            {
                                binding.button = GAMEPAD_BUTTON_X;
                            }
                            else if (control == action_map_format::gamepad_y)
                            {
                                binding.button = GAMEPAD_BUTTON_Y;
                            }
                            else if (control == action_map_format::gamepad_back || control == action_map_format::gamepad_select)
                            {
                                binding.button = GAMEPAD_BUTTON_BACK;
                            }
                            else if (control == action_map_format::gamepad_start)
                            {
                                binding.button = GAMEPAD_BUTTON_START;
                            }
                            else if (control == action_map_format::gamepad_dpad_up || control == action_map_format::gamepad_dpad_up_alias)
                            {
                                binding.button = GAMEPAD_BUTTON_DPAD_UP;
                            }
                            else if (control == action_map_format::gamepad_dpad_down || control == action_map_format::gamepad_dpad_down_alias)
                            {
                                binding.button = GAMEPAD_BUTTON_DPAD_DOWN;
                            }
                            else if (control == action_map_format::gamepad_dpad_left || control == action_map_format::gamepad_dpad_left_alias)
                            {
                                binding.button = GAMEPAD_BUTTON_DPAD_LEFT;
                            }
                            else if (control == action_map_format::gamepad_dpad_right || control == action_map_format::gamepad_dpad_right_alias)
                            {
                                binding.button = GAMEPAD_BUTTON_DPAD_RIGHT;
                            }
                            else if (control == action_map_format::gamepad_left_shoulder || control == action_map_format::gamepad_left_shoulder_alias || control == action_map_format::gamepad_left_shoulder_short)
                            {
                                binding.button = GAMEPAD_BUTTON_LEFT_SHOULDER;
                            }
                            else if (control == action_map_format::gamepad_right_shoulder || control == action_map_format::gamepad_right_shoulder_alias || control == action_map_format::gamepad_right_shoulder_short)
                            {
                                binding.button = GAMEPAD_BUTTON_RIGHT_SHOULDER;
                            }
                            else if (control == action_map_format::gamepad_left_thumb || control == action_map_format::gamepad_left_thumb_alias || control == action_map_format::gamepad_left_thumb_short)
                            {
                                binding.button = GAMEPAD_BUTTON_LEFT_THUMB;
                            }
                            else if (control == action_map_format::gamepad_right_thumb || control == action_map_format::gamepad_right_thumb_alias || control == action_map_format::gamepad_right_thumb_short)
                            {
                                binding.button = GAMEPAD_BUTTON_RIGHT_THUMB;
                            }
                            parsed_binding = binding.button != BUTTON_NONE;
                        }
                    }

                    if (!binding.action_name.empty() && parsed_binding)
                    {
                        if (action_states.find(binding.action_name) == action_states.end())
                        {
                            action_states[binding.action_name] = {};
                        }
                        action_bindings.push_back(binding);
                    }
                    archive.EndObject();
                }
                archive.EndItem();
            }
            archive.EndArray();
        }

        archive.EndObject();
        return !archive.HasError();
    }

    void ClearActionMap()
    {
        action_map_version = 1;
        action_map_name.clear();
        action_bindings.clear();
        action_states.clear();
    }

    bool IsDown(Button button)
    {
        const uint32 button_index = static_cast<uint32>(button);
        if (!input_active || input_suppressed || button_index <= static_cast<uint32>(BUTTON_NONE) || button_index >= static_cast<uint32>(BUTTON_COUNT))
        {
            return false;
        }

        if (button == MOUSE_BUTTON_LEFT)
        {
            return mouse.left_button_press;
        }
        if (button == MOUSE_BUTTON_RIGHT)
        {
            return mouse.right_button_press;
        }
        if (button == MOUSE_BUTTON_MIDDLE)
        {
            return mouse.middle_button_press;
        }
        if (button_index >= static_cast<uint32>('0') && button_index <= static_cast<uint32>('9'))
        {
            return keyboard.digits[button_index - static_cast<uint32>('0')];
        }
        if (button_index >= static_cast<uint32>('A') && button_index <= static_cast<uint32>('Z'))
        {
            return keyboard.characters[button_index - static_cast<uint32>('A')];
        }
        if (button_index >= static_cast<uint32>(KEYBOARD_BUTTON_UP) && button_index <= static_cast<uint32>(KEYBOARD_BUTTON_ALTGR))
        {
            return keyboard.buttons[button_index - static_cast<uint32>(KEYBOARD_BUTTON_UP)];
        }
        if (button_index >= static_cast<uint32>(GAMEPAD_BUTTON_A) && button_index <= static_cast<uint32>(GAMEPAD_BUTTON_RIGHT_THUMB))
        {
            const uint32 gamepad_button_index = button_index - static_cast<uint32>(GAMEPAD_BUTTON_A);
            for (uint32 gamepad_index = 0; gamepad_index < max_gamepad_count; ++gamepad_index)
            {
                if (gamepads[gamepad_index].buttons[gamepad_button_index])
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool IsPressed(Button button)
    {
        const uint32 button_index = static_cast<uint32>(button);
        if (!input_active || input_suppressed || button_index <= static_cast<uint32>(BUTTON_NONE) || button_index >= static_cast<uint32>(BUTTON_COUNT))
        {
            return false;
        }

        if (button == MOUSE_BUTTON_LEFT)
        {
            return mouse.left_button_press && !previous_mouse.left_button_press;
        }
        if (button == MOUSE_BUTTON_RIGHT)
        {
            return mouse.right_button_press && !previous_mouse.right_button_press;
        }
        if (button == MOUSE_BUTTON_MIDDLE)
        {
            return mouse.middle_button_press && !previous_mouse.middle_button_press;
        }
        if (button_index >= static_cast<uint32>('0') && button_index <= static_cast<uint32>('9'))
        {
            const uint32 index = button_index - static_cast<uint32>('0');
            return keyboard.digits[index] && !previous_keyboard.digits[index];
        }
        if (button_index >= static_cast<uint32>('A') && button_index <= static_cast<uint32>('Z'))
        {
            const uint32 index = button_index - static_cast<uint32>('A');
            return keyboard.characters[index] && !previous_keyboard.characters[index];
        }
        if (button_index >= static_cast<uint32>(KEYBOARD_BUTTON_UP) && button_index <= static_cast<uint32>(KEYBOARD_BUTTON_ALTGR))
        {
            const uint32 index = button_index - static_cast<uint32>(KEYBOARD_BUTTON_UP);
            return keyboard.buttons[index] && !previous_keyboard.buttons[index];
        }
        if (button_index >= static_cast<uint32>(GAMEPAD_BUTTON_A) && button_index <= static_cast<uint32>(GAMEPAD_BUTTON_RIGHT_THUMB))
        {
            const uint32 gamepad_button_index = button_index - static_cast<uint32>(GAMEPAD_BUTTON_A);
            bool current_down = false;
            bool previous_down = false;
            for (uint32 gamepad_index = 0; gamepad_index < max_gamepad_count; ++gamepad_index)
            {
                current_down = current_down || gamepads[gamepad_index].buttons[gamepad_button_index];
                previous_down = previous_down || previous_gamepads[gamepad_index].buttons[gamepad_button_index];
            }
            return current_down && !previous_down;
        }

        return false;
    }

    bool IsReleased(Button button)
    {
        const uint32 button_index = static_cast<uint32>(button);
        if (!input_active || input_suppressed || button_index <= static_cast<uint32>(BUTTON_NONE) || button_index >= static_cast<uint32>(BUTTON_COUNT))
        {
            return false;
        }

        if (button == MOUSE_BUTTON_LEFT)
        {
            return !mouse.left_button_press && previous_mouse.left_button_press;
        }
        if (button == MOUSE_BUTTON_RIGHT)
        {
            return !mouse.right_button_press && previous_mouse.right_button_press;
        }
        if (button == MOUSE_BUTTON_MIDDLE)
        {
            return !mouse.middle_button_press && previous_mouse.middle_button_press;
        }
        if (button_index >= static_cast<uint32>('0') && button_index <= static_cast<uint32>('9'))
        {
            const uint32 index = button_index - static_cast<uint32>('0');
            return !keyboard.digits[index] && previous_keyboard.digits[index];
        }
        if (button_index >= static_cast<uint32>('A') && button_index <= static_cast<uint32>('Z'))
        {
            const uint32 index = button_index - static_cast<uint32>('A');
            return !keyboard.characters[index] && previous_keyboard.characters[index];
        }
        if (button_index >= static_cast<uint32>(KEYBOARD_BUTTON_UP) && button_index <= static_cast<uint32>(KEYBOARD_BUTTON_ALTGR))
        {
            const uint32 index = button_index - static_cast<uint32>(KEYBOARD_BUTTON_UP);
            return !keyboard.buttons[index] && previous_keyboard.buttons[index];
        }
        if (button_index >= static_cast<uint32>(GAMEPAD_BUTTON_A) && button_index <= static_cast<uint32>(GAMEPAD_BUTTON_RIGHT_THUMB))
        {
            const uint32 gamepad_button_index = button_index - static_cast<uint32>(GAMEPAD_BUTTON_A);
            bool current_down = false;
            bool previous_down = false;
            for (uint32 gamepad_index = 0; gamepad_index < max_gamepad_count; ++gamepad_index)
            {
                current_down = current_down || gamepads[gamepad_index].buttons[gamepad_button_index];
                previous_down = previous_down || previous_gamepads[gamepad_index].buttons[gamepad_button_index];
            }
            return !current_down && previous_down;
        }

        return false;
    }

    bool IsDoubleClicked()
    {
        return !input_suppressed && double_click;
    }

    void SetDoubleClickInterval(double seconds)
    {
        double_click_interval = seconds;
    }

    Button GetButtonFromString(StringView value)
    {
        if (value.empty())
        {
            return BUTTON_NONE;
        }

        String key(value);
        key = utils::ToUpper(key);
        if (key.size() == 1)
        {
            const char c = key[0];
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            {
                return static_cast<Button>(c);
            }
        }

        if (key == action_map_format::keyboard_space) return KEYBOARD_BUTTON_SPACE;
        if (key == action_map_format::keyboard_enter) return KEYBOARD_BUTTON_ENTER;
        if (key == action_map_format::keyboard_escape || key == action_map_format::keyboard_escape_alias) return KEYBOARD_BUTTON_ESCAPE;
        if (key == action_map_format::keyboard_tab) return KEYBOARD_BUTTON_TAB;
        if (key == action_map_format::keyboard_up) return KEYBOARD_BUTTON_UP;
        if (key == action_map_format::keyboard_down) return KEYBOARD_BUTTON_DOWN;
        if (key == action_map_format::keyboard_left) return KEYBOARD_BUTTON_LEFT;
        if (key == action_map_format::keyboard_right) return KEYBOARD_BUTTON_RIGHT;
        if (key == action_map_format::keyboard_right_shift || key == action_map_format::keyboard_right_shift_alias) return KEYBOARD_BUTTON_RSHIFT;
        if (key == action_map_format::keyboard_left_shift || key == action_map_format::keyboard_left_shift_alias || key == action_map_format::keyboard_shift) return KEYBOARD_BUTTON_LSHIFT;
        if (key == action_map_format::keyboard_right_control || key == action_map_format::keyboard_right_control_alias) return KEYBOARD_BUTTON_RCONTROL;
        if (key == action_map_format::keyboard_left_control || key == action_map_format::keyboard_left_control_alias || key == action_map_format::keyboard_control || key == action_map_format::keyboard_control_alias) return KEYBOARD_BUTTON_LCONTROL;
        if (key == action_map_format::keyboard_alt) return KEYBOARD_BUTTON_ALT;
        if (key == action_map_format::keyboard_altgr) return KEYBOARD_BUTTON_ALTGR;
        if (key == action_map_format::keyboard_f1) return KEYBOARD_BUTTON_F1;
        if (key == action_map_format::keyboard_f2) return KEYBOARD_BUTTON_F2;
        if (key == action_map_format::keyboard_f3) return KEYBOARD_BUTTON_F3;
        if (key == action_map_format::keyboard_f4) return KEYBOARD_BUTTON_F4;
        if (key == action_map_format::keyboard_f5) return KEYBOARD_BUTTON_F5;
        if (key == action_map_format::keyboard_f6) return KEYBOARD_BUTTON_F6;
        if (key == action_map_format::keyboard_f7) return KEYBOARD_BUTTON_F7;
        if (key == action_map_format::keyboard_f8) return KEYBOARD_BUTTON_F8;
        if (key == action_map_format::keyboard_f9) return KEYBOARD_BUTTON_F9;
        if (key == action_map_format::keyboard_f10) return KEYBOARD_BUTTON_F10;
        if (key == action_map_format::keyboard_f11) return KEYBOARD_BUTTON_F11;
        if (key == action_map_format::keyboard_f12) return KEYBOARD_BUTTON_F12;
        if (key == action_map_format::keyboard_home) return KEYBOARD_BUTTON_HOME;
        if (key == action_map_format::keyboard_insert) return KEYBOARD_BUTTON_INSERT;
        if (key == action_map_format::keyboard_delete) return KEYBOARD_BUTTON_DELETE;
        if (key == action_map_format::keyboard_backspace) return KEYBOARD_BUTTON_BACKSPACE;
        if (key == action_map_format::keyboard_page_down || key == action_map_format::keyboard_page_down_alias) return KEYBOARD_BUTTON_PAGEDOWN;
        if (key == action_map_format::keyboard_page_up || key == action_map_format::keyboard_page_up_alias) return KEYBOARD_BUTTON_PAGEUP;
        if (key == action_map_format::keyboard_numpad0) return KEYBOARD_BUTTON_NUMPAD0;
        if (key == action_map_format::keyboard_numpad1) return KEYBOARD_BUTTON_NUMPAD1;
        if (key == action_map_format::keyboard_numpad2) return KEYBOARD_BUTTON_NUMPAD2;
        if (key == action_map_format::keyboard_numpad3) return KEYBOARD_BUTTON_NUMPAD3;
        if (key == action_map_format::keyboard_numpad4) return KEYBOARD_BUTTON_NUMPAD4;
        if (key == action_map_format::keyboard_numpad5) return KEYBOARD_BUTTON_NUMPAD5;
        if (key == action_map_format::keyboard_numpad6) return KEYBOARD_BUTTON_NUMPAD6;
        if (key == action_map_format::keyboard_numpad7) return KEYBOARD_BUTTON_NUMPAD7;
        if (key == action_map_format::keyboard_numpad8) return KEYBOARD_BUTTON_NUMPAD8;
        if (key == action_map_format::keyboard_numpad9) return KEYBOARD_BUTTON_NUMPAD9;
        if (key == action_map_format::keyboard_multiply) return KEYBOARD_BUTTON_MULTIPLY;
        if (key == action_map_format::keyboard_add) return KEYBOARD_BUTTON_ADD;
        if (key == action_map_format::keyboard_separator) return KEYBOARD_BUTTON_SEPARATOR;
        if (key == action_map_format::keyboard_subtract) return KEYBOARD_BUTTON_SUBTRACT;
        if (key == action_map_format::keyboard_decimal) return KEYBOARD_BUTTON_DECIMAL;
        if (key == action_map_format::keyboard_divide) return KEYBOARD_BUTTON_DIVIDE;
        if (key == action_map_format::keyboard_tilde) return KEYBOARD_BUTTON_TILDE;
        if (key == action_map_format::mouse_left_path || key == action_map_format::mouse_left_path_alias) return MOUSE_BUTTON_LEFT;
        if (key == action_map_format::mouse_right_path || key == action_map_format::mouse_right_path_alias) return MOUSE_BUTTON_RIGHT;
        if (key == action_map_format::mouse_middle_path || key == action_map_format::mouse_middle_path_alias) return MOUSE_BUTTON_MIDDLE;
        if (key == action_map_format::gamepad_back || key == action_map_format::gamepad_select) return GAMEPAD_BUTTON_BACK;
        if (key == action_map_format::gamepad_start) return GAMEPAD_BUTTON_START;
        if (key == action_map_format::gamepad_dpad_up || key == action_map_format::gamepad_dpad_up_alias) return GAMEPAD_BUTTON_DPAD_UP;
        if (key == action_map_format::gamepad_dpad_down || key == action_map_format::gamepad_dpad_down_alias) return GAMEPAD_BUTTON_DPAD_DOWN;
        if (key == action_map_format::gamepad_dpad_left || key == action_map_format::gamepad_dpad_left_alias) return GAMEPAD_BUTTON_DPAD_LEFT;
        if (key == action_map_format::gamepad_dpad_right || key == action_map_format::gamepad_dpad_right_alias) return GAMEPAD_BUTTON_DPAD_RIGHT;
        if (key == action_map_format::gamepad_left_shoulder || key == action_map_format::gamepad_left_shoulder_alias || key == action_map_format::gamepad_left_shoulder_short) return GAMEPAD_BUTTON_LEFT_SHOULDER;
        if (key == action_map_format::gamepad_right_shoulder || key == action_map_format::gamepad_right_shoulder_alias || key == action_map_format::gamepad_right_shoulder_short) return GAMEPAD_BUTTON_RIGHT_SHOULDER;
        if (key == action_map_format::gamepad_left_thumb || key == action_map_format::gamepad_left_thumb_alias || key == action_map_format::gamepad_left_thumb_short) return GAMEPAD_BUTTON_LEFT_THUMB;
        if (key == action_map_format::gamepad_right_thumb || key == action_map_format::gamepad_right_thumb_alias || key == action_map_format::gamepad_right_thumb_short) return GAMEPAD_BUTTON_RIGHT_THUMB;
        return BUTTON_NONE;
    }

    const InputActionState* GetActionState(StringView action_name)
    {
        if (input_suppressed)
        {
            return nullptr;
        }
        auto it = action_states.find(String(action_name));
        return it == action_states.end() ? nullptr : &it->second.state;
    }

    bool IsActionDown(StringView action_name)
    {
        const InputActionState* state = GetActionState(action_name);
        return state && state->down;
    }

    bool IsActionPressed(StringView action_name)
    {
        const InputActionState* state = GetActionState(action_name);
        return state && state->pressed;
    }

    bool IsActionReleased(StringView action_name)
    {
        const InputActionState* state = GetActionState(action_name);
        return state && state->released;
    }

    float GetActionValue(StringView action_name)
    {
        const InputActionState* state = GetActionState(action_name);
        return state ? state->value : 0.0f;
    }

    float2 GetActionAxis2D(StringView action_name)
    {
        const InputActionState* state = GetActionState(action_name);
        return state ? state->axis : float2(0, 0);
    }

    const String& GetTextInput()
    {
        static const String empty_text_input;
        return input_suppressed ? empty_text_input : text_input;
    }

    void SetInputSuppressed(bool suppressed)
    {
        input_suppressed = suppressed;
    }

    bool IsInputSuppressed()
    {
        return input_suppressed;
    }

    const KeyboardState& GetKeyboardState()
    {
        static const KeyboardState neutral_keyboard;
        return input_suppressed ? neutral_keyboard : keyboard;
    }

    const MouseState& GetMouseState()
    {
        if (!input_suppressed)
        {
            return mouse;
        }

        static MouseState neutral_mouse;
        neutral_mouse = MouseState();
        neutral_mouse.position = mouse.position;
        return neutral_mouse;
    }

    const GamepadState* GetGamepadState(uint32 device_index)
    {
        static const GamepadState neutral_gamepad;
        if (device_index >= max_gamepad_count)
        {
            return nullptr;
        }
        return input_suppressed ? &neutral_gamepad : &gamepads[device_index];
    }
}
