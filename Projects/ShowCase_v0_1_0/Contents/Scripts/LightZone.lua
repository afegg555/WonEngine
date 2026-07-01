-- LightZone.lua
-- Trigger zone: count unique crates pushed in; when all are in, ignite the brazier
-- (ember particles + one-shot audio) and flag the objective complete via game_data.

local script = {}

local REQUIRED = 1

function script.OnCreate(self)
    self.entered = {}
    self.count = 0
    self.lit = false
    won.game_data.set_int("crates_in", 0)
    won.game_data.set_bool("complete", false)
end

local function is_crate(name)
    return name ~= nil and string.sub(name, 1, 5) == "Crate"
end

function script.OnTriggerEnter3D(self, other)
    local name = won.entity.get_name(other)
    if is_crate(name) and not self.entered[other] then
        self.entered[other] = true
        self.count = self.count + 1
        won.game_data.set_int("crates_in", self.count)
        won.log.info("LightZone: crate in (" .. self.count .. "/" .. REQUIRED .. ")")
        if self.count >= REQUIRED and not self.lit then
            self.lit = true
            won.game_data.set_bool("complete", true)
            local brazier = won.scene.find_by_name("Brazier")
            if brazier then
                won.particle_emitter_3d.set_active(brazier, true)
            end
            local fire = won.scene.find_by_name("BrazierFire")
            if fire then
                won.audio_source.play(fire)
            end
            local clear_sound = won.scene.find_by_name("ClearSound")
            if clear_sound then
                won.audio_source.play(clear_sound)
            end
            won.log.info("LightZone: complete - brazier lit")
        end
    end
end

function script.OnTriggerExit3D(self, other)
    if self.entered[other] then
        self.entered[other] = nil
        self.count = math.max(0, self.count - 1)
        won.game_data.set_int("crates_in", self.count)
    end
end

return script
