#pragma once
#include "RuntimeExport.h"
#include "Types.h"

namespace won::console
{
    class WONENGINE_API ConsoleOverlay
    {
    public:
        void Update();
        void Draw(float viewport_width, float viewport_height);
        bool IsOpen() const;

    private:
        bool open = false;
        String input_line;
        Vector<String> history;
        int history_index = -1;
    };
}
