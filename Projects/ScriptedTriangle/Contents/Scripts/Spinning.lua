local script = {}

function script.OnCreate(self)
    self.time = 0.0
    won.log.info("Spinning script loaded")
end

function script.OnUpdate(self, dt)
    self.time = self.time + dt
    local scale = 1.0 + math.sin(self.time * 3.0) * 0.15
    won.transform.set_scale(scale, scale, scale)
    won.transform.rotate_euler(0.0, dt * 1.25, 0.0)
end

return script
