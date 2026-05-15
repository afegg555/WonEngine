local script = {}

function script.OnCreate(self)
    won.log.info("OnCreate entity: " .. tostring(self.entity) .. " (" .. won.entity.get_name(self.entity) .. ")")
end

function script.OnUpdate(self, dt)
end

function script.OnDestroy(self)
    won.log.info("OnDestroy entity: " .. tostring(self.entity) .. " (" .. won.entity.get_name(self.entity) .. ")")
end

return script
