#pragma once

#include <chrono>
#include <functional>
#include <utility>

namespace won::utils
{
    class Timer
    {
    public:
        Timer()
        {
            Reset();
        }

        void Reset()
        {
            start_time = std::chrono::steady_clock::now();
        }

        double ElapsedSeconds() const
        {
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsed = now - start_time;
            return elapsed.count();
        }

        double ElapsedMilliSeconds() const
        {
            return ElapsedSeconds() * 1000.0;
        }

    private:
        std::chrono::steady_clock::time_point start_time;
    };
}
