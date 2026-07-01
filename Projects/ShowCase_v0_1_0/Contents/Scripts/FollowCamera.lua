-- FollowCamera.lua
-- Third-person spring-arm camera. Orbits the Player with the Look action, follows smoothly,
-- and publishes its yaw to game_data so the controller can do camera-relative movement.

local script = {}

local DISTANCE = 4.5
local PITCH_INIT = 0.25
local ROT_SPEED = 2.2
local FOLLOW_LERP = 10.0
local TARGET_HEIGHT = 1.1
local BASE_HEIGHT = 0.4

function script.OnCreate(self)
    local fx, fy, fz = won.transform.get_forward()
    self.yaw = math.atan(fx, fz)
    self.pitch = PITCH_INIT
    self.player = won.scene.find_by_name("Player")
end

function script.OnUpdate(self, dt)
    if not self.player then
        self.player = won.scene.find_by_name("Player")
    end
    if not self.player then
        return
    end

    local look_x, look_y = won.input.get_action_axis2d("Look")
    self.yaw = self.yaw + look_x * ROT_SPEED * dt
    self.pitch = math.max(-0.1, math.min(1.0, self.pitch - look_y * ROT_SPEED * dt))
    won.game_data.set_float("cam_yaw", self.yaw)

    local px, py, pz = won.transform.get_position(self.player)
    local tx, ty, tz = px, py + TARGET_HEIGHT, pz

    local sy, cy = math.sin(self.yaw), math.cos(self.yaw)
    local cp, sp = math.cos(self.pitch), math.sin(self.pitch)
    local cam_x = tx - sy * cp * DISTANCE
    local cam_y = ty + sp * DISTANCE + BASE_HEIGHT
    local cam_z = tz - cy * cp * DISTANCE

    local ox, oy, oz = won.transform.get_position()
    local t = math.min(1.0, FOLLOW_LERP * dt)
    local nx = ox + (cam_x - ox) * t
    local ny = oy + (cam_y - oy) * t
    local nz = oz + (cam_z - oz) * t
    won.transform.set_position(nx, ny, nz)

    local dx, dy, dz = tx - nx, ty - ny, tz - nz
    local dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    if dist > 0.001 then
        local look_yaw = math.atan(dx, dz)
        local look_pitch = math.asin(math.max(-1.0, math.min(1.0, -dy / dist)))
        won.transform.set_euler(look_pitch, look_yaw, 0.0)
    end
end

return script
