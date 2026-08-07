-- FreeCameraController.lua
-- A reusable free-fly camera controller for WonEngine examples.
-- Attach this script to the Camera entity in any scene.
--
-- Input is driven by the project action map (Contents/Config/Input.woninput):
--   Move     (AXIS2D): WASD / left stick          -> forward-back + strafe
--   Look     (AXIS2D): arrow keys / right stick    -> yaw + pitch
--   Vertical (AXIS1D): Q/E / LT-RT triggers        -> down / up
--   Fast     (BUTTON): Left Shift / right shoulder -> move faster
-- Rebind by editing the action map, not this script. If a gamepad axis feels
-- inverted, flip the "scale" on that binding in Input.woninput.
--
-- Movement uses the camera's actual forward (won.transform.get_forward), so it
-- works with ANY initial rotation on the Camera entity. No assumed start pitch.

local script = {}

local MOVE_SPEED = 10.0
local FAST_MULT = 5.0
local ROTATE_SPEED = 1.6
local MAX_PITCH = math.pi * 0.5 - 0.02

function script.OnCreate(self)
    won.log.info("FreeCameraController: attached")
end

function script.OnUpdate(self, dt)
    local look_x, look_y = won.input.get_action_axis2d("Look")
    local dyaw = look_x * ROTATE_SPEED * dt
    local dpitch = look_y * ROTATE_SPEED * dt

    if dyaw ~= 0.0 or dpitch ~= 0.0 then
        local fx, fy, fz = won.transform.get_forward()
        local cur_pitch = math.asin(math.max(-1.0, math.min(1.0, -fy)))
        local new_pitch = math.max(-MAX_PITCH, math.min(MAX_PITCH, cur_pitch + dpitch))
        won.transform.rotate_euler(new_pitch - cur_pitch, dyaw, 0.0)
    end

    local speed = MOVE_SPEED
    if won.input.is_action_down("Fast") then
        speed = MOVE_SPEED * FAST_MULT
    end

    local move_x, move_y = won.input.get_action_axis2d("Move")
    local move_side = move_x * speed * dt
    local move_fwd = move_y * speed * dt
    local move_up = won.input.get_action_value("Vertical") * speed * dt

    if move_fwd == 0.0 and move_side == 0.0 and move_up == 0.0 then
        return
    end

    local fx, fy, fz = won.transform.get_forward()
    local rx = fz
    local rz = -fx
    local rlen = math.sqrt(rx * rx + rz * rz)
    if rlen > 1e-5 then
        rx = rx / rlen
        rz = rz / rlen
    end

    local dx = fx * move_fwd + rx * move_side
    local dy = fy * move_fwd + move_up
    local dz = fz * move_fwd + rz * move_side

    won.transform.translate(dx, dy, dz)
end

function script.OnDestroy(self)
end

return script
