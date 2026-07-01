-- TPController.lua
-- Third-person physics character. Drives a dynamic rigidbody via velocity (camera-relative),
-- turns toward the movement direction with angular velocity, and cross-fades locomotion clips.

local script = {}

local MOVE_SPEED = 3.2
local RUN_MULT = 1.9
local TURN_GAIN = 12.0
local RUN_THRESHOLD = 0.9
local IDLE_DANCE_DELAY = 3.0
local JUMP_SPEED = 5.2
local GROUND_VY = 0.5
local JUMP_WINDUP = 0.35
local SETTLE_VY = 0.3
local SETTLE_TIME = 0.14
local JUMP_MAX_AIR = 3.0

function script.OnCreate(self)
    self.cam = won.scene.find_by_name("Startup Camera")
    self.idle_time = 0.0
    self.jumping = false
    self.windup = 0.0
    self.settle = 0.0
    self.air_time = 0.0
    won.log.info("TPController attached")
end

function script.OnUpdate(self, dt)
    if not self.cam then
        self.cam = won.scene.find_by_name("Startup Camera")
    end

    local move_x, move_y = won.input.get_action_axis2d("Move")
    local mag = math.sqrt(move_x * move_x + move_y * move_y)
    local fast = won.input.is_action_down("Fast")
    local speed = MOVE_SPEED * (fast and RUN_MULT or 1.0)

    -- camera-relative movement: read the camera's forward direction directly
    local cam_yaw = 0.0
    if self.cam then
        local cfx, cfy, cfz = won.transform.get_forward(self.cam)
        if cfx ~= nil then
            cam_yaw = math.atan(cfx, cfz)
        end
    end
    local sy, cy = math.sin(cam_yaw), math.cos(cam_yaw)
    local dir_x = move_y * sy + move_x * cy
    local dir_z = move_y * cy - move_x * sy

    local vx = dir_x * speed
    local vz = dir_z * speed
    local _, vy = won.rigidbody.get_velocity()

    -- jump: only from the ground (single explicit airborne state, no apex flicker)
    -- anticipation: start the windup now, delay the launch so the clip's takeoff lines up
    local grounded = (not self.jumping) and math.abs(vy) < GROUND_VY
    if grounded and won.input.is_action_pressed("Jump") then
        self.jumping = true
        self.windup = JUMP_WINDUP
        self.settle = 0.0
        self.air_time = 0.0
    end

    if self.jumping and self.windup > 0.0 then
        self.windup = self.windup - dt
        if self.windup <= 0.0 then
            vy = JUMP_SPEED
        end
    end

    -- after launch, land once vertical motion settles (rests on the ground OR an obstacle).
    -- apex passes through ~0 only briefly, so it never accumulates enough settle time.
    if self.jumping and self.windup <= 0.0 then
        self.air_time = self.air_time + dt
        if math.abs(vy) < SETTLE_VY then
            self.settle = self.settle + dt
        else
            self.settle = 0.0
        end
        if self.settle > SETTLE_TIME or self.air_time > JUMP_MAX_AIR then
            self.jumping = false
        end
    end

    won.rigidbody.set_velocity(vx, vy, vz)

    if mag > 0.05 then
        self.idle_time = 0.0
        -- turn toward movement via angular velocity (keeps the body upright: x/z = 0)
        -- model front faces -Z, so offset the facing yaw by 180 degrees
        local target_yaw = math.atan(vx, vz) + math.pi
        local fx, fy, fz = won.transform.get_forward()
        local cur_yaw = math.atan(fx, fz)
        local dyaw = target_yaw - cur_yaw
        while dyaw > math.pi do dyaw = dyaw - 2.0 * math.pi end
        while dyaw < -math.pi do dyaw = dyaw + 2.0 * math.pi end
        won.rigidbody.set_angular_velocity(0.0, dyaw * TURN_GAIN, 0.0)
    else
        won.rigidbody.set_angular_velocity(0.0, 0.0, 0.0)
    end

    if self.jumping then
        self.idle_time = 0.0
        won.animation.play_by_name("Jumping", 0.12)
    elseif mag > 0.05 then
        if fast and mag > RUN_THRESHOLD then
            won.animation.play_by_name("Fast_Run", 0.15)
        else
            won.animation.play_by_name("Walking", 0.15)
        end
    else
        self.idle_time = self.idle_time + dt
        if self.idle_time >= IDLE_DANCE_DELAY then
            won.animation.play_by_name("Hip_Hop_Dancing", 0.4)
        else
            won.animation.play_by_name("Breathing_Idle", 0.25)
        end
    end
end

return script
