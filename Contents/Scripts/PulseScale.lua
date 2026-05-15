local script = {}

function script.OnCreate(self)
    self.time = 0.0
end

function script.OnUpdate(self, dt)
    self.time = self.time + dt

    local scale = 1.0 + math.sin(self.time * 3.0) * 0.25
    won.transform.set_scale(scale, scale, scale)
end

return script
