local script = {}

local presets = {
    Clear = {
        sun_intensity = 100000.0,
        ambient_intensity = 10000.0,
        ambient_color = { 0.03, 0.03, 0.035 },
        turbidity = 2.0,
        mie_eccentricity = 0.8,
        cloud_coverage = 0.15,
        cloud_density = 0.8,
        cloud_color = { 1.0, 1.0, 1.0 },
    },
    Cloudy = {
        sun_intensity = 55000.0,
        ambient_intensity = 14000.0,
        ambient_color = { 0.04, 0.042, 0.05 },
        turbidity = 5.0,
        mie_eccentricity = 0.7,
        cloud_coverage = 0.62,
        cloud_density = 1.0,
        cloud_color = { 0.97, 0.98, 1.0 },
    },
    Rain = {
        sun_intensity = 22000.0,
        ambient_intensity = 9000.0,
        ambient_color = { 0.035, 0.038, 0.045 },
        turbidity = 9.0,
        mie_eccentricity = 0.6,
        cloud_coverage = 0.9,
        cloud_density = 1.0,
        cloud_color = { 0.72, 0.74, 0.80 },
    },
}

local default_preset = "Clear"
local default_blend_duration = 3.0
local cloud_frequency = 4.0
local cloud_speed = 0.03
local cloud_direction = { 1.0, 0.35 }

local current = nil
local blend_from = nil
local blend_to = nil
local blend_duration = 0.0
local blend_elapsed = 0.0
local requested_preset = nil
local requested_blend_duration = nil
local applied = false

local function copy_preset(preset)
    return {
        sun_intensity = preset.sun_intensity,
        ambient_intensity = preset.ambient_intensity,
        ambient_color = { preset.ambient_color[1], preset.ambient_color[2], preset.ambient_color[3] },
        turbidity = preset.turbidity,
        mie_eccentricity = preset.mie_eccentricity,
        cloud_coverage = preset.cloud_coverage,
        cloud_density = preset.cloud_density,
        cloud_color = { preset.cloud_color[1], preset.cloud_color[2], preset.cloud_color[3] },
    }
end

local function mix(a, b, t)
    return a + (b - a) * t
end

local function blend_preset(from, to, t)
    return {
        sun_intensity = mix(from.sun_intensity, to.sun_intensity, t),
        ambient_intensity = mix(from.ambient_intensity, to.ambient_intensity, t),
        ambient_color = {
            mix(from.ambient_color[1], to.ambient_color[1], t),
            mix(from.ambient_color[2], to.ambient_color[2], t),
            mix(from.ambient_color[3], to.ambient_color[3], t),
        },
        turbidity = mix(from.turbidity, to.turbidity, t),
        mie_eccentricity = mix(from.mie_eccentricity, to.mie_eccentricity, t),
        cloud_coverage = mix(from.cloud_coverage, to.cloud_coverage, t),
        cloud_density = mix(from.cloud_density, to.cloud_density, t),
        cloud_color = {
            mix(from.cloud_color[1], to.cloud_color[1], t),
            mix(from.cloud_color[2], to.cloud_color[2], t),
            mix(from.cloud_color[3], to.cloud_color[3], t),
        },
    }
end

local function push_to_environment(state)
    won.environment.set_sun_intensity(state.sun_intensity)
    won.environment.set_ambient(state.ambient_intensity, state.ambient_color[1], state.ambient_color[2], state.ambient_color[3])
    won.environment.set_atmosphere(state.turbidity, state.mie_eccentricity)
    won.environment.set_cloud(state.cloud_coverage, state.cloud_density)
    won.environment.set_cloud_color(state.cloud_color[1], state.cloud_color[2], state.cloud_color[3])
end

function script.OnCreate(self)
    won.environment.set_cloud_motion(cloud_frequency, cloud_speed, cloud_direction[1], cloud_direction[2])
    current = copy_preset(presets[default_preset])
    applied = false
    requested_preset = default_preset

    won.event.subscribe("won.weather.set", function(payload)
        if payload == nil then
            return
        end
        local name, duration = string.match(tostring(payload), "^([%a]+):([%d%.]+)$")
        if name == nil then
            name = tostring(payload)
            duration = nil
        end
        if presets[name] == nil then
            return
        end
        requested_preset = name
        requested_blend_duration = duration and tonumber(duration) or nil
    end)
end

function script.OnUpdate(self, dt)
    if requested_preset ~= nil then
        local target = presets[requested_preset]
        local duration = requested_blend_duration or default_blend_duration
        if applied and duration > 0.0 then
            blend_from = current
            blend_to = copy_preset(target)
            blend_duration = duration
            blend_elapsed = 0.0
        else
            current = copy_preset(target)
            blend_from = nil
            blend_to = nil
        end
        won.event.fire("won.weather.changed", requested_preset)
        requested_preset = nil
        requested_blend_duration = nil
    end

    if blend_to ~= nil then
        blend_elapsed = blend_elapsed + dt
        local t = blend_elapsed / blend_duration
        if t >= 1.0 then
            t = 1.0
        end
        current = blend_preset(blend_from, blend_to, t)
        if t >= 1.0 then
            blend_from = nil
            blend_to = nil
        end
    end

    if current ~= nil then
        push_to_environment(current)
        applied = true
    end
end

return script
