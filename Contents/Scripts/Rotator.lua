local script = {}

function script.OnUpdate(self, dt)
    won.transform.rotate_euler(0.0, dt, 0.0)
end

return script
