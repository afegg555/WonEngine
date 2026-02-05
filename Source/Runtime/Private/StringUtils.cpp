#include "StringUtils.h"

#include <algorithm>
#include <cctype>

namespace won::utils
{
    WString ToWideString(const String& str)
    {
        WString wstr;
        size_t i = 0;
        while (i < str.size())
        {
            uint32_t codepoint = 0;
            unsigned char c = str[i];

            if (c < 0x80)
            {
                codepoint = c;
                i += 1;
            }
            else if ((c & 0xE0) == 0xC0)
            {
                if (i + 1 >= str.size())
                    break;
                codepoint = ((c & 0x1F) << 6) | (str[i + 1] & 0x3F);
                i += 2;
            }
            else if ((c & 0xF0) == 0xE0)
            {
                if (i + 2 >= str.size())
                    break;
                codepoint = ((c & 0x0F) << 12) | ((str[i + 1] & 0x3F) << 6) | (str[i + 2] & 0x3F);
                i += 3;
            }
            else if ((c & 0xF8) == 0xF0)
            {
                if (i + 3 >= str.size())
                    break;
                codepoint = ((c & 0x07) << 18) | ((str[i + 1] & 0x3F) << 12) | ((str[i + 2] & 0x3F) << 6) | (str[i + 3] & 0x3F);
                i += 4;
            }
            else
            {
                ++i;
                continue;
            }

            if constexpr (sizeof(wchar_t) >= 4)
            {
                wstr += static_cast<wchar_t>(codepoint);
            }
            else
            {
                if (codepoint <= 0xFFFF)
                {
                    wstr += static_cast<wchar_t>(codepoint);
                }
                else
                {
                    codepoint -= 0x10000;
                    wstr += static_cast<wchar_t>((codepoint >> 10) + 0xD800);
                    wstr += static_cast<wchar_t>((codepoint & 0x3FF) + 0xDC00);
                }
            }
        }
        return wstr;
    }

    String ToString(const WString& wstr)
    {
        String str;
        for (size_t i = 0; i < wstr.size(); ++i)
        {
            uint32_t codepoint = 0;
            wchar_t wc = wstr[i];

            if constexpr (sizeof(wchar_t) >= 4)
            {
                codepoint = static_cast<uint32_t>(wc);
            }
            else
            {
                if (wc >= 0xD800 && wc <= 0xDBFF)
                {
                    if (i + 1 < wstr.size())
                    {
                        wchar_t wc_low = wstr[i + 1];
                        if (wc_low >= 0xDC00 && wc_low <= 0xDFFF)
                        {
                            codepoint = ((static_cast<uint32_t>(wc - 0xD800) << 10) | (static_cast<uint32_t>(wc_low - 0xDC00))) + 0x10000;
                            ++i;
                        }
                    }
                }
                else
                {
                    codepoint = static_cast<uint32_t>(wc);
                }
            }

            if (codepoint <= 0x7F)
            {
                str += static_cast<char>(codepoint);
            }
            else if (codepoint <= 0x7FF)
            {
                str += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
                str += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            else if (codepoint <= 0xFFFF)
            {
                str += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
                str += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                str += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            else if (codepoint <= 0x10FFFF)
            {
                str += static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
                str += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                str += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                str += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
        }
        return str;
    }

    String ToUpper(StringView input)
    {
        String output(input);
        std::transform(output.begin(), output.end(), output.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::toupper(ch));
        });
        return output;
    }

    String ToLower(StringView input)
    {
        String output(input);
        std::transform(output.begin(), output.end(), output.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        return output;
    }

    uint64 Hash(StringView input)
    {
        const uint64 fnv_offset_basis = 14695981039346656037ull;
        const uint64 fnv_prime = 1099511628211ull;

        uint64 hash = fnv_offset_basis;
        for (unsigned char ch : input)
        {
            hash ^= static_cast<uint64>(ch);
            hash *= fnv_prime;
        }

        return hash;
    }
}
