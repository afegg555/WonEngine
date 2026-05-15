local script = {}

local speed = 3.0

function script.OnUpdate(self, dt)
    local step = speed * dt

    if won.input.is_key_down("W") then
        won.transform.translate(0.0, 0.0, step)
    end

    if won.input.is_key_down("S") then
        won.transform.translate(0.0, 0.0, -step)
    end

    if won.input.is_key_down("A") then
        won.transform.translate(-step, 0.0, 0.0)
    end

    if won.input.is_key_down("D") then
        won.transform.translate(step, 0.0, 0.0)
    end
end

return script
