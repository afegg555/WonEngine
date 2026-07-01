-- ClearOverlay.lua
-- On objective complete: fade the clear image in (alpha 0 -> 1),
-- hold for a few seconds, then fade it back out (alpha 1 -> 0).

local script = {}

local FADE_IN = 1.8
local HOLD_TIME = 3.0
local FADE_OUT = 1.8

function script.OnCreate(self)
    self.phase = "idle"
    self.t = 0.0
    won.material.fork()
    won.material.set_base_color(1.0, 1.0, 1.0, 0.0)
end

function script.OnUpdate(self, dt)
    if self.phase == "idle" then
        if won.game_data.get_bool("complete") then
            self.phase = "in"
            self.t = 0.0
        else
            return
        end
    end

    self.t = self.t + dt

    if self.phase == "in" then
        local a = math.min(1.0, self.t / FADE_IN)
        won.material.set_base_color(1.0, 1.0, 1.0, a)
        if self.t >= FADE_IN then
            self.phase = "hold"
            self.t = 0.0
        end
    elseif self.phase == "hold" then
        if self.t >= HOLD_TIME then
            self.phase = "out"
            self.t = 0.0
        end
    elseif self.phase == "out" then
        local a = math.max(0.0, 1.0 - self.t / FADE_OUT)
        won.material.set_base_color(1.0, 1.0, 1.0, a)
        if self.t >= FADE_OUT then
            self.phase = "done"
            won.material.set_base_color(1.0, 1.0, 1.0, 0.0)
        end
    end
end

return script
