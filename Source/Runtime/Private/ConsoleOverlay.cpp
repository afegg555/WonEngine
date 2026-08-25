#include "ConsoleOverlay.h"

#include "Backlog.h"
#include "BuiltinFont.h"
#include "Console.h"
#include "DebugDraw.h"
#include "Input.h"
#include "Platform.h"

#include <algorithm>

#ifndef WON_SHIPPING
namespace won::console
{
    namespace
    {
        constexpr float console_text_scale = 1.0f;
        constexpr const char* console_prompt = ">>> ";
        constexpr uint32 console_background_color = debugdraw::PackRGBA8(0, 0, 0, 200);
        constexpr uint32 console_separator_color = debugdraw::PackRGBA8(90, 90, 90, 255);
        constexpr uint32 console_input_color = debugdraw::PackRGBA8(255, 255, 255);
        constexpr uint32 console_output_color = debugdraw::PackRGBA8(200, 200, 200);
    }

    void ConsoleOverlay::Update()
    {
        if (io::IsPressed(io::KEYBOARD_BUTTON_TILDE))
        {
            open = !open;
        }

        if (open)
        {
            const bool control_down = io::IsDown(io::KEYBOARD_BUTTON_LCONTROL) || io::IsDown(io::KEYBOARD_BUTTON_RCONTROL);

            for (const char c : io::GetTextInput())
            {
                if (c == '`' || c == '~')
                {
                    continue;
                }
                if (c == '\b')
                {
                    if (!input_line.empty())
                    {
                        input_line.pop_back();
                    }
                    continue;
                }
                if (control_down)
                {
                    continue;
                }
                if (c >= 0x20 && c < 0x7f)
                {
                    input_line += c;
                }
            }

            if (control_down && io::IsPressed(io::Button('V')))
            {
                String clipboard_text;
                if (platform::GetClipboardText(clipboard_text))
                {
                    for (const char c : clipboard_text)
                    {
                        if (c >= 0x20 && c < 0x7f)
                        {
                            input_line += c;
                        }
                    }
                }
            }

            if (io::IsPressed(io::KEYBOARD_BUTTON_TAB) && !input_line.empty())
            {
                const Vector<String> candidates = FindNamesWithPrefix(input_line);
                if (candidates.size() == 1)
                {
                    input_line = candidates[0] + ' ';
                }
                else if (!candidates.empty())
                {
                    Size common = candidates[0].size();
                    for (Size i = 1; i < candidates.size(); ++i)
                    {
                        common = (std::min)(common, candidates[i].size());
                        while (common > 0 && candidates[i].compare(0, common, candidates[0], 0, common) != 0)
                        {
                            --common;
                        }
                    }
                    input_line = candidates[0].substr(0, common);

                    String line;
                    for (const String& candidate : candidates)
                    {
                        line += line.empty() ? candidate : "  " + candidate;
                    }
                    backlog::Post(line);
                }
            }

            if (io::IsPressed(io::KEYBOARD_BUTTON_ENTER) && !input_line.empty())
            {
                backlog::Post(console_prompt + input_line);
                Execute(input_line);
                history.push_back(input_line);
                input_line.clear();
                history_index = -1;
            }

            if (io::IsPressed(io::KEYBOARD_BUTTON_UP) && !history.empty())
            {
                if (history_index < 0)
                {
                    history_index = static_cast<int>(history.size()) - 1;
                }
                else if (history_index > 0)
                {
                    --history_index;
                }
                input_line = history[history_index];
            }

            if (io::IsPressed(io::KEYBOARD_BUTTON_DOWN) && history_index >= 0)
            {
                ++history_index;
                if (history_index >= static_cast<int>(history.size()))
                {
                    history_index = -1;
                    input_line.clear();
                }
                else
                {
                    input_line = history[history_index];
                }
            }
        }

        io::SetInputSuppressed(open);
    }

    void ConsoleOverlay::Draw(float viewport_width, float viewport_height)
    {
        if (!open)
        {
            return;
        }

        const float line_height = static_cast<float>(builtinfont::glyph_height) * console_text_scale;
        const float panel_height = viewport_height * 0.5f;
        const bool anchor_bottom = anchor == OverlayAnchor::BottomLeft || anchor == OverlayAnchor::BottomRight;
        const float panel_top = anchor_bottom ? (viewport_height - panel_height) : 0.0f;
        const float input_y = panel_top + panel_height - line_height - 4.0f;

        debugdraw::Rect2D({ 0.0f, panel_top }, { viewport_width, panel_height }, console_background_color);
        debugdraw::Rect2D({ 0.0f, input_y - 3.0f }, { viewport_width, 1.0f }, console_separator_color);
        debugdraw::Text2D({ 4.0f, input_y }, (console_prompt + input_line).c_str(), console_input_color, console_text_scale);

        const int max_lines = static_cast<int>((input_y - panel_top - 4.0f) / line_height);
        if (max_lines <= 0)
        {
            return;
        }

        const float glyph_advance = static_cast<float>(builtinfont::glyph_width) * console_text_scale;
        const Size max_chars = glyph_advance > 0.0f ? static_cast<Size>((viewport_width - 8.0f) / glyph_advance) : 0;

        const Vector<String> entries = backlog::GetRecentLines(static_cast<Size>(max_lines));
        Vector<String> visual_lines;
        for (const String& entry : entries)
        {
            Size start = 0;
            while (true)
            {
                const Size newline = entry.find('\n', start);
                const Size end = newline == String::npos ? entry.size() : newline;
                Size segment_start = start;
                while (max_chars > 0 && end - segment_start > max_chars)
                {
                    visual_lines.push_back(entry.substr(segment_start, max_chars));
                    segment_start += max_chars;
                }
                visual_lines.push_back(entry.substr(segment_start, end - segment_start));
                if (newline == String::npos)
                {
                    break;
                }
                start = newline + 1;
            }
        }

        float y = input_y - line_height;
        for (int i = static_cast<int>(visual_lines.size()) - 1; i >= 0 && y >= panel_top; --i)
        {
            debugdraw::Text2D({ 4.0f, y }, visual_lines[i].c_str(), console_output_color, console_text_scale);
            y -= line_height;
        }
    }

    bool ConsoleOverlay::IsOpen() const
    {
        return open;
    }
}
#endif
