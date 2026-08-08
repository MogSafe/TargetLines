_addon.name = 'TargetLines'
_addon.author = 'MogSafe'
_addon.version = '1.0.0'
_addon.commands = {'targetlines', 'tl'}

config = require('config')
texts = require('texts')
require('logger')

local defaults = {}
defaults.enabled = true
defaults.debug = false
defaults.scan_range = 50
defaults.write_interval = 0.016
defaults.action_timeout = 1.5
defaults.opacity = 0.8
defaults.opacity_scale = 1.0
defaults.player_opacity_scale = 0.70
defaults.ally_opacity_scale = 0.70
defaults.enemy_opacity_scale = 0.70
defaults.fade_scale = 1.0
defaults.width_scale = 1.0
defaults.glow_scale = 1.0
defaults.fan_opacity_scale = 0.55
defaults.action_debug = false
defaults.source_height_scale = 1.0
defaults.target_height_scale = 1.0
defaults.pair_cooldown = 8.0
defaults.special_cooldown = 0.0
defaults.regular_attack_once = true
defaults.regular_attack_mode = 'first'
defaults.show_player_lines = true
defaults.show_party_lines = true
defaults.show_enemy_lines = true
defaults.show_pet_lines = true
defaults.show_fan_lines = true
defaults.show_special_lines = true
defaults.show_other_party_lines = false
defaults.color_blind_mode = false
defaults.claim_fallback = false
defaults.claim_timeout = 0.5
defaults.auto_inspect = false
defaults.auto_inspect_interval = 120
defaults.display = {}
defaults.display.pos = {x = 160, y = 220}
defaults.display.bg = {red = 0, green = 0, blue = 0, alpha = 150}
defaults.display.text = {font = 'Consolas', size = 10, red = 255, green = 255, blue = 255, alpha = 255}

local settings = config.load(defaults)
if settings.show_other_party_lines == nil then
    settings.show_other_party_lines = defaults.show_other_party_lines
end
if settings.debug ~= false then
    settings.debug = false
end
if tonumber(settings.action_timeout) == 2.0 then
    settings.action_timeout = defaults.action_timeout
end
if tonumber(settings.write_interval) == 0.05 then
    settings.write_interval = defaults.write_interval
end
if tonumber(settings.special_cooldown) == 4.0 then
    settings.special_cooldown = defaults.special_cooldown
end
if settings.regular_attack_mode ~= 'first' and settings.regular_attack_mode ~= 'repeat' and settings.regular_attack_mode ~= 'off' then
    settings.regular_attack_mode = settings.regular_attack_once == false and 'repeat' or defaults.regular_attack_mode
end
for _, opacity_key in ipairs({'player_opacity_scale', 'ally_opacity_scale', 'enemy_opacity_scale'}) do
    local saved_opacity = tonumber(settings[opacity_key])
    if saved_opacity == 1.0 then
        settings[opacity_key] = defaults[opacity_key]
    elseif saved_opacity == 0.75 then
        settings[opacity_key] = defaults[opacity_key]
    elseif saved_opacity == 0.40 then
        settings[opacity_key] = 0.25
    end
end
if tonumber(settings.glow_scale) == 0.60 then
    settings.glow_scale = 0.50
elseif tonumber(settings.glow_scale) == 1.30 then
    settings.glow_scale = 2.00
end
config.save(settings)

local box = texts.new('${current_string}', settings.display, settings)
box.current_string = ''
box:hide()
local config_box = texts.new('${current_string}', settings.display, settings)
config_box.current_string = ''
config_box:hide()
local config_visible = false

local state_dir = windower.windower_path .. 'plugins/settings/TargetLines'
local state_path = state_dir .. '/lines.json'
local inspect_path = windower.addon_path .. 'inspect.log'
local runtime_log_path = windower.addon_path .. 'runtime.log'
local last_write = 0
local last_signature = ''
local last_lines = {}
local last_nearby = {}
local recent_lines = {}
local seen_pairs = {}
local seen_special_pairs = {}
local recent_spell_starts = {}
local recent_spell_events = {}
local recent_ability_starts = {}
local probe_lines = {}
local line_sequence = 0
local update_config_box
local append_runtime_log
local action_debug_log
local mouse_capture = nil
local boneprobe_until = 0
local duplicate_finish_window = 5.0
local auto_inspect_last = 0
local auto_inspect_zone = nil
local auto_inspect_zone_written = false
local native_probe_commands = {
    device = true,
    renderstats = true,
    matrixprobe = true,
    matrixnext = true,
    ffxiprobe = true,
    luamobprobe = true,
    dynamicbone = true,
    drawtest = true,
    drawon = true,
    drawoff = true,
}

local function ensure_state_dir()
    if type(windower.create_dir) == 'function' then
        windower.create_dir(windower.windower_path .. 'plugins/settings')
        windower.create_dir(state_dir)
    end
end

local slider_rows = {
    {name = 'opacity_scale', command = 'opacity', label = 'opacity'},
    {name = 'fade_scale', command = 'fade', label = 'fade'},
    {name = 'width_scale', command = 'width', label = 'width'},
    {name = 'glow_scale', command = 'glow', label = 'glow'},
    {name = 'source_height_scale', command = 'sourceheight', label = 'source ht'},
    {name = 'target_height_scale', command = 'targetheight', label = 'target ht'},
}

local preset_rows = {
    width_scale = {
        {'Thin', 0.65},
        {'Normal', 0.80},
        {'Thick', 1.00},
    },
    player_opacity_scale = {
        {'Low', 0.25},
        {'Normal', 0.70},
        {'High', 1.25},
    },
    ally_opacity_scale = {
        {'Low', 0.25},
        {'Normal', 0.70},
        {'High', 1.25},
    },
    enemy_opacity_scale = {
        {'Low', 0.25},
        {'Normal', 0.70},
        {'High', 1.25},
    },
    glow_scale = {
        {'Low', 0.50},
        {'Normal', 1.00},
        {'High', 2.00},
    },
    fade_scale = {
        {'Short', 0.80},
        {'Normal', 1.00},
        {'Long', 1.25},
    },
    fan_opacity_scale = {
        {'Low', 0.35},
        {'Normal', 0.55},
        {'High', 0.75},
    },
    regular_attack_mode = {
        {'First Only', 'first'},
        {'Repeat After Delay', 'repeat'},
        {'Off', 'off'},
    },
}

local config_rows = {
    {type = 'toggle', name = 'enabled', label = 'Enable Lines'},
    {type = 'toggle', name = 'show_player_lines', label = 'Player Lines'},
    {type = 'toggle', name = 'show_party_lines', label = 'Party/Trust Lines'},
    {type = 'toggle', name = 'show_pet_lines', label = 'Pet Lines'},
    {type = 'toggle', name = 'show_enemy_lines', label = 'Enemy Lines'},
    {type = 'toggle', name = 'show_other_party_lines', label = 'Other Party Lines'},
    {type = 'toggle', name = 'show_special_lines', label = 'Abilities/Spells'},
    {type = 'toggle', name = 'show_fan_lines', label = 'AoE Fan Lines'},
    {type = 'toggle', name = 'color_blind_mode', label = 'Color Blind Mode'},
    {type = 'choice', name = 'width_scale', label = 'Line Width'},
    {type = 'choice', name = 'player_opacity_scale', label = 'Player Opacity'},
    {type = 'choice', name = 'ally_opacity_scale', label = 'Ally Opacity'},
    {type = 'choice', name = 'enemy_opacity_scale', label = 'Enemy Opacity'},
    -- Omitted from the UI for now because the effect is too subtle to justify the extra setting.
    -- {type = 'choice', name = 'glow_scale', label = 'Line Glow'},
    {type = 'choice', name = 'fade_scale', label = 'Line Duration'},
    {type = 'choice', name = 'fan_opacity_scale', label = 'AoE Fan Opacity'},
    {type = 'choice', name = 'regular_attack_mode', label = 'Regular Attacks'},
}

local function clamp(value, minimum, maximum)
    value = tonumber(value)
    if not value then
        return minimum
    end

    return math.max(minimum, math.min(maximum, value))
end

local function slider_scale(name)
    return clamp(settings[name], 0.5, 2.0)
end

local function regular_mode()
    local mode = tostring(settings.regular_attack_mode or ''):lower()
    if mode == 'first' or mode == 'repeat' or mode == 'off' then
        return mode
    end

    return settings.regular_attack_once == false and 'repeat' or 'first'
end

local function slider_bar(name)
    local scale = slider_scale(name)
    local slots = 20
    local filled = math.floor(((scale - 0.5) / 1.5) * slots + 0.5)
    filled = math.max(0, math.min(slots, filled))
    return ('[-] [%s%s] [+] %3d%%'):format(string.rep('=', filled), string.rep('-', slots - filled), math.floor(scale * 100 + 0.5))
end

local function adjust_slider(name, delta)
    settings[name] = clamp(slider_scale(name) + delta, 0.5, 2.0)
    config.save(settings)
    last_signature = ''
    update_config_box()
end

local function preset_index(name)
    local presets = preset_rows[name]
    if not presets then
        return 1
    end

    local current = settings[name]
    if type(current) == 'string' then
        current = current:lower()
    else
        current = tonumber(current)
    end

    local best_index = 1
    local best_delta = nil
    for index, preset in ipairs(presets) do
        local value = preset[2]
        if type(value) == 'string' then
            if tostring(current) == value then
                return index
            end
        else
            local delta = math.abs((tonumber(current) or value) - value)
            if not best_delta or delta < best_delta then
                best_delta = delta
                best_index = index
            end
        end
    end

    return best_index
end

local function preset_label(name)
    local presets = preset_rows[name]
    local preset = presets and presets[preset_index(name)]
    return preset and preset[1] or tostring(settings[name])
end

local function color_text(text, red, green, blue)
    return ('\\cs(%u,%u,%u)%s\\cr'):format(red, green, blue, tostring(text))
end

local function disabled_value(text)
    return color_text(text, 135, 135, 135)
end

local function detail_value(text)
    return color_text(text, 185, 220, 255)
end

local config_title_width = 50
local config_label_width = 30

local function setting_detail(name)
    if name == 'width_scale' then
        local normal_width = 0.80
        return ('%d%%'):format(math.floor((slider_scale(name) / normal_width) * 100 + 0.5))
    elseif name == 'glow_scale' then
        return ('%d%%'):format(math.floor(slider_scale(name) * 100 + 0.5))
    elseif name == 'player_opacity_scale' or name == 'ally_opacity_scale' or name == 'enemy_opacity_scale' then
        local alpha = clamp(defaults.opacity * slider_scale('opacity_scale') * slider_scale(name), 0, 1)
        return ('%d%%'):format(math.floor(alpha * 100 + 0.5))
    elseif name == 'fade_scale' then
        return ('%.2fs'):format(defaults.action_timeout * slider_scale('fade_scale'))
    elseif name == 'fan_opacity_scale' then
        return ('%d%%'):format(math.floor((tonumber(settings.fan_opacity_scale) or defaults.fan_opacity_scale) * 100 + 0.5))
    elseif name == 'regular_attack_mode' and regular_mode() == 'repeat' then
        return ('%.1fs'):format(tonumber(settings.pair_cooldown) or defaults.pair_cooldown)
    end

    return nil
end

local function setting_label(row)
    local detail = setting_detail(row.name)
    if detail then
        return ('%s %s'):format(row.label, detail_value(('(%s)'):format(detail)))
    end

    return row.label
end

local function setting_label_visible_length(row)
    local detail = setting_detail(row.name)
    return #row.label + (detail and (#detail + 3) or 0)
end

local function setting_label_column(row)
    local label = setting_label(row)
    local padding = math.max(1, config_label_width - setting_label_visible_length(row))
    return label .. string.rep(' ', padding)
end

local function centered_text(text, width)
    text = tostring(text or '')
    local padding = width - #text
    if padding <= 0 then
        return text
    end

    local left = math.floor(padding / 2)
    local right = padding - left
    return string.rep(' ', left) .. text .. string.rep(' ', right)
end

local function set_preset(name, direction)
    local presets = preset_rows[name]
    if not presets then
        return
    end

    local index = preset_index(name) + direction
    index = math.max(1, math.min(#presets, index))
    settings[name] = presets[index][2]
    if name == 'regular_attack_mode' then
        settings.regular_attack_once = settings.regular_attack_mode ~= 'repeat'
        seen_pairs = {}
    end

    config.save(settings)
    last_signature = ''
    update_config_box()
end

local function toggle_setting(name)
    settings[name] = not settings[name]
    config.save(settings)
    last_signature = ''
    update_config_box()
end

local function set_boolean_from_arg(name, arg)
    local value = arg and arg:lower() or nil
    if value == 'on' or value == '1' or value == 'true' or value == 'yes' then
        settings[name] = true
    elseif value == 'off' or value == '0' or value == 'false' or value == 'no' then
        settings[name] = false
    else
        settings[name] = not settings[name]
    end

    config.save(settings)
    last_signature = ''
    update_config_box()
    return settings[name]
end

local function set_regular_mode(mode)
    settings.regular_attack_mode = mode
    settings.regular_attack_once = mode ~= 'repeat'
    seen_pairs = {}
    config.save(settings)
    last_signature = ''
    update_config_box()
end

local function adjust_opacity_command(command_name, setting_name, display_name, value)
    if value == '+' then
        adjust_slider(setting_name, 0.01)
    elseif value == '-' then
        adjust_slider(setting_name, -0.01)
    else
        local scale = tonumber(value)
        if not (scale and scale >= 0.5 and scale <= 2.0) then
            warning('Usage: //tl ' .. command_name .. ' <0.5-2>|+|-')
            return
        end

        settings[setting_name] = scale
        config.save(settings)
        last_signature = ''
        update_config_box()
    end

    log(('TargetLines %s set to %d%%.'):format(display_name, math.floor(slider_scale(setting_name) * 100 + 0.5)))
end

local function effective_timeout()
    return defaults.action_timeout * slider_scale('fade_scale')
end

local function effective_source_height()
    return -0.75 * slider_scale('source_height_scale')
end

local function effective_target_height()
    return -1.35 * slider_scale('target_height_scale')
end

local function config_control_at_mouse(x, y)
    if not config_visible then
        return nil
    end

    local origin_x = tonumber(settings.display.pos.x) or defaults.display.pos.x
    local origin_y = tonumber(settings.display.pos.y) or defaults.display.pos.y
    local row_height = 16
    local row = math.floor((y - origin_y - 4) / row_height) + 1
    local rel_x = x - origin_x

    if row == 1 and rel_x >= 260 and rel_x <= 520 then
        return {type = 'close'}
    end

    local setting = config_rows[row - 2]
    if not setting then
        return nil
    end

    if setting.type == 'toggle' then
        if rel_x >= 238 and rel_x <= 550 then
            return {type = 'toggle', setting = setting}
        end
    elseif setting.type == 'choice' then
        if rel_x >= 238 and rel_x <= 338 then
            return {type = 'choice', setting = setting, delta = -1}
        elseif rel_x >= 378 and rel_x <= 580 then
            return {type = 'choice', setting = setting, delta = 1}
        end
    end

    return nil
end

local function handle_mouse(type, x, y)
    if type == 1 then
        mouse_capture = config_control_at_mouse(x, y)
        return mouse_capture ~= nil
    elseif type == 0 then
        return mouse_capture ~= nil
    elseif type == 2 then
        if not mouse_capture then
            return false
        end

        local pressed = mouse_capture
        mouse_capture = nil
        local released = config_control_at_mouse(x, y)
        if released and released.type == pressed.type then
            if released.type == 'close' then
                config_visible = false
                update_config_box()
            elseif released.setting.name == pressed.setting.name and released.type == 'toggle' then
                toggle_setting(released.setting.name)
            elseif released.setting.name == pressed.setting.name and released.delta == pressed.delta then
                set_preset(released.setting.name, released.delta)
            end
        end
        return true
    end

    return false
end

local special_action_categories = {
    [3] = true,  -- weapon skill
    [4] = true,  -- spell/magic action
    [6] = true,  -- job ability
    [7] = true,
    [8] = true,  -- spell/magic finish on some packets
    [11] = true, -- monster TP move / special action
    [13] = true, -- pet/avatar ability
    [14] = true,
    [15] = true,
}

local default_colors = {
    player = 0xF040F0FF,
    friendly = 0xF088FF9A,
    enemy = 0xF0FF4A55,
    hostile_support = 0xF0FF22F0,
    npc = 0xE800E8FF,
    claim_enemy = 0xD8FF4A55,
}

local color_blind_colors = {
    player = 0xF056B4E9,          -- sky blue
    friendly = 0xF0F0E442,        -- yellow
    enemy = 0xF0D55E00,           -- vermilion/orange
    hostile_support = 0xF0CC79A7, -- reddish purple
    npc = 0xE856B4E9,
    claim_enemy = 0xD8D55E00,
}

local function active_colors()
    return settings.color_blind_mode and color_blind_colors or default_colors
end

local function scale_color_alpha(color, scale)
    color = tonumber(color) or 0xEFFFFFFF
    scale = clamp(scale, 0, 2)
    local alpha = math.floor(color / 0x1000000)
    local rgb = color % 0x1000000
    return rgb + math.floor(clamp(alpha * scale, 0, 255) + 0.5) * 0x1000000
end

local function json_escape(value)
    return tostring(value):gsub('\\', '\\\\')
        :gsub('"', '\\"')
        :gsub('\b', '\\b')
        :gsub('\f', '\\f')
        :gsub('\n', '\\n')
        :gsub('\r', '\\r')
        :gsub('\t', '\\t')
end

local function json_string(value)
    if value == nil then
        return 'null'
    end

    return '"' .. json_escape(value) .. '"'
end

local function primary_model(mob)
    if type(mob and mob.models) == 'table' then
        return tonumber(mob.models[1]) or 0
    end

    return tonumber(mob and mob.model) or 0
end

local function model_list(mob)
    if type(mob and mob.models) ~= 'table' then
        return tostring(primary_model(mob))
    end

    local models = {}
    for index = 1, math.min(#mob.models, 10) do
        models[#models + 1] = tostring(mob.models[index])
    end

    return table.concat(models, ',')
end

local normal_anchor_models = {
    [272] = true, -- Death Jacket
    [3110] = true, -- Shantotto II / Shantotto-style trust model
}

local short_anchor_models = {
    [268] = true, -- Sand Hare
    [276] = true, -- Thread Leech
    [340] = true, -- Brutal Sheep
    [348] = true, -- Beach Pugil
    [352] = true, -- Beach Monk
    [356] = true, -- Snipper / crab-style models
    [494] = true, -- Goblin Gambler
    [497] = true, -- Goblin Bounty Hunter
    [572] = true, -- Ghoul
    [960] = true, -- Barnacled Box
    [1299] = true, -- Houu the Shoalwader
}

local function short_anchor(mob)
    local race = tonumber(mob and mob.race) or 0
    local name = tostring(mob and mob.name or ''):lower()
    local model = primary_model(mob)
    local model_size = tonumber(mob and mob.model_size) or 0

    if normal_anchor_models[model] then
        return false
    end

    if short_anchor_models[model] then
        return true
    end

    if race == 5 or race == 6 then
        return true
    end

    if name:find('automaton', 1, true) then
        return true
    end

    if name:find('sabertooth', 1, true) or name:find('tiger', 1, true) or name:find('smilodon', 1, true) then
        return true
    end

    if name:find('crawler', 1, true) or name:find('caterpillar', 1, true) then
        return true
    end

    return mob and mob.is_npc and model_size > 0 and model_size <= 1.0
end

local floating_models = {}

local function floating_anchor(mob)
    if not (mob and mob.is_npc) then
        return false
    end

    local model = primary_model(mob)
    if floating_models[model] then
        return true
    end

    local name = tostring(mob.name or ''):lower()
    if name:find('bee', 1, true) or name:find('wasp', 1, true) or name:find('vespo', 1, true) then
        return true
    end

    return false
end

local function mob_distance(mob)
    local distance = tonumber(mob and mob.distance) or 0
    if distance > 100 then
        return math.sqrt(distance)
    end

    return distance
end

local function get_mobs()
    if type(windower.ffxi.get_mob_array) == 'function' then
        local ok, mobs = pcall(windower.ffxi.get_mob_array)
        if ok and type(mobs) == 'table' then
            return mobs
        end
    end

    local mobs = {}
    if type(windower.ffxi.get_mob_by_index) ~= 'function' then
        return mobs
    end

    for index = 0, 2303 do
        local ok, mob = pcall(windower.ffxi.get_mob_by_index, index)
        if ok and mob then
            mobs[index] = mob
        end
    end

    return mobs
end

local function is_visible_entity(mob)
    return mob and mob.id and mob.id > 0 and mob.index and mob.hpp and mob.hpp > 0
end

local function is_visible_npc(mob)
    return is_visible_entity(mob) and mob.is_npc
end

local function first_nonzero(...)
    for index = 1, select('#', ...) do
        local value = select(index, ...)
        local number = tonumber(value)
        if number and number ~= 0 then
            return number
        end
    end

    return 0
end

local function player_point(player)
    local me = windower.ffxi.get_mob_by_target('me') or {}
    player = player or windower.ffxi.get_player() or {}

    return {
        id = me.id or player.id or 0,
        index = me.index or player.index or 0,
        name = me.name or player.name or '',
        x = me.x or player.x or 0,
        y = me.y or player.y or 0,
        z = me.z or player.z or 0,
        hpp = me.hpp or player.vitals and player.vitals.hpp or 100,
        facing = me.facing or player.facing or 0,
        is_npc = false,
        race = first_nonzero(me.race, player.race),
        model = first_nonzero(me.model, player.model),
        models = me.models or player.models,
        model_size = first_nonzero(me.model_size, player.model_size),
        model_scale = first_nonzero(me.model_scale, player.model_scale, 1),
    }
end

local function party_ids()
    local ids = {}
    local party = windower.ffxi.get_party()

    for _, key in ipairs({'p0', 'p1', 'p2', 'p3', 'p4', 'p5', 'a10', 'a11', 'a12', 'a13', 'a14', 'a15', 'a20', 'a21', 'a22', 'a23', 'a24', 'a25'}) do
        local member = party and party[key]
        local id = member and ((member.mob and member.mob.id) or member.id)
        if id then
            ids[id] = true
        end
        if member and member.mob and member.mob.index then
            ids['index:' .. tostring(member.mob.index)] = true
        end
        if member and member.mob then
            for _, pet_key in ipairs({'pet_index', 'fellow_index'}) do
                local pet_index = tonumber(member.mob[pet_key])
                if pet_index and pet_index > 0 then
                    ids['index:' .. tostring(pet_index)] = true
                    ids['petindex:' .. tostring(pet_index)] = true
                    local pet = windower.ffxi.get_mob_by_index(pet_index)
                    if pet and pet.id and pet.id > 0 then
                        ids[pet.id] = true
                        ids['pet:' .. tostring(pet.id)] = true
                    end
                end
            end
        end
    end

    local player = windower.ffxi.get_player()
    if player and player.id then
        ids[player.id] = true
        ids._player_id = player.id
    end

    return ids, player
end

local function party_member_point_by_id(id)
    if not id then
        return nil
    end

    local party = windower.ffxi.get_party()
    for _, key in ipairs({'p0', 'p1', 'p2', 'p3', 'p4', 'p5', 'a10', 'a11', 'a12', 'a13', 'a14', 'a15', 'a20', 'a21', 'a22', 'a23', 'a24', 'a25'}) do
        local member = party and party[key]
        local mob = member and member.mob
        if mob and mob.id == id then
            return mob
        end
    end

    local player = windower.ffxi.get_player()
    if player and player.id == id then
        return player_point(player)
    end

    return nil
end

local function enrich_from_party(source)
    if not source or not source.id then
        return source
    end

    local party_mob = party_member_point_by_id(source.id)
    if not party_mob then
        return source
    end

    if not source.race or source.race == 0 then
        source.race = party_mob.race
    end
    if not source.model or source.model == 0 then
        source.model = party_mob.model
    end
    source.models = source.models or party_mob.models
    if not source.model_size or source.model_size == 0 then
        source.model_size = party_mob.model_size
    end
    if not source.model_scale or source.model_scale == 0 then
        source.model_scale = party_mob.model_scale
    end
    return source
end

local function mob_by_id_or_index(id, index)
    if index then
        local mob = windower.ffxi.get_mob_by_index(index)
        if mob and (not id or mob.id == id) then
            return mob
        end
    end

    local mobs = get_mobs()
    for _, mob in pairs(mobs) do
        if mob and mob.id == id then
            return mob
        end
    end

    return nil
end

local function classify_line(source, target, party)
    local colors = active_colors()
    local source_party = source and ((source.id and party[source.id]) or (source.index and party['index:' .. tostring(source.index)]))
    local target_party = target and ((target.id and party[target.id]) or (target.index and party['index:' .. tostring(target.index)]))

    if source_party and target_party then
        return 'friendly', colors.friendly
    end

    if source_party then
        return 'player', colors.player
    end

    if source and source.is_npc and target_party then
        return 'enemy', colors.enemy
    end

    if source and source.is_npc and target and target.is_npc then
        return 'hostile_support', colors.hostile_support
    end

    if source and source.is_npc then
        return 'npc', colors.npc
    end

    return 'player', colors.player
end

local function line_source_role(source, party)
    local source_id = source and source.id
    local source_index = source and source.index
    if source_id and party._player_id and source_id == party._player_id then
        return 'player'
    end

    if (source_id and party['pet:' .. tostring(source_id)])
        or (source_index and party['petindex:' .. tostring(source_index)]) then
        return 'pet'
    end

    local source_party = source and ((source.id and party[source.id]) or (source.index and party['index:' .. tostring(source.index)]))
    if source_party then
        return 'party'
    end

    if source and source.is_npc then
        return 'enemy'
    end

    return 'player'
end

local function party_entity(mob, party)
    return mob and ((mob.id and party[mob.id])
        or (mob.index and party['index:' .. tostring(mob.index)])
        or (mob.id and party['pet:' .. tostring(mob.id)])
        or (mob.index and party['petindex:' .. tostring(mob.index)]))
end

local function line_allowed(source, target, party, special, fan)
    if fan and settings.show_fan_lines == false then
        return false, 'fan_lines_disabled'
    end

    if special and settings.show_special_lines == false then
        return false, 'special_lines_disabled'
    end

    if settings.show_other_party_lines == false and not party_entity(source, party) and not party_entity(target, party) then
        return false, 'other_party_lines_disabled'
    end

    local role = line_source_role(source, party)
    if role == 'player' and settings.show_player_lines == false then
        return false, 'player_lines_disabled'
    elseif role == 'party' and settings.show_party_lines == false then
        return false, 'party_lines_disabled'
    elseif role == 'pet' and settings.show_pet_lines == false then
        return false, 'pet_lines_disabled'
    elseif role == 'enemy' and settings.show_enemy_lines == false then
        return false, 'enemy_lines_disabled'
    end

    return true, role
end

local function role_opacity_scale(role)
    if role == 'player' then
        return slider_scale('player_opacity_scale')
    elseif role == 'party' or role == 'pet' then
        return slider_scale('ally_opacity_scale')
    elseif role == 'enemy' then
        return slider_scale('enemy_opacity_scale')
    end

    return slider_scale('player_opacity_scale')
end

local function is_special_action(packet)
    local category = tonumber(packet and (packet.category or packet.Category))
    return special_action_categories[category] or false
end

local function action_category(packet)
    return tonumber(packet and (packet.category or packet.Category)) or 0
end

local function action_spell_id(category, packet, action_target)
    local first_action = action_target and action_target.actions and action_target.actions[1] or nil
    if category == 8 then
        return tonumber(first_action and (first_action.param or first_action.Param)) or tonumber(packet and (packet.param or packet.Param or packet['Param'])) or 0
    end

    return tonumber(packet and (packet.param or packet.Param or packet['Param'])) or tonumber(first_action and (first_action.param or first_action.Param)) or 0
end

local function add_recent_line(source, target, kind, color, timeout, options)
    if not is_visible_entity(source) or not is_visible_entity(target) or source.id == target.id then
        return false, 'invalid_or_same_entity'
    end

    options = options or {}
    local pair_key = tostring(source.id) .. '>' .. tostring(target.id)
    local now = os.clock()
    if options.always_draw and options.special_cooldown then
        local cooldown = tonumber(settings.special_cooldown) or defaults.special_cooldown
        if cooldown > 0 and seen_special_pairs[pair_key] and now - seen_special_pairs[pair_key] < cooldown then
            return false, 'special_cooldown'
        end

        if cooldown > 0 then
            seen_special_pairs[pair_key] = now
        end
    end

    if not options.always_draw then
        local mode = regular_mode()
        if mode == 'off' then
            return false, 'regular_off'
        elseif mode == 'first' then
            if seen_pairs[pair_key] then
                return false, 'regular_seen'
            end
        elseif mode == 'repeat' then
            local cooldown = tonumber(settings.pair_cooldown) or defaults.pair_cooldown
            if seen_pairs[pair_key] and now - seen_pairs[pair_key] < cooldown then
                return false, 'regular_repeat_delay'
            end
        end
    end

    if not options.always_draw then
        seen_pairs[pair_key] = now
    end
    line_sequence = line_sequence + 1
    local key = options.always_draw and (pair_key .. '#' .. tostring(line_sequence)) or pair_key
    recent_lines[key] = {
        uid = line_sequence,
        source = source,
        target = target,
        kind = kind or 'player',
        color = color or 0xEFFFFFFF,
        created = now,
        timeout = timeout or effective_timeout(),
    }
    probe_lines = {recent_lines[key]}
    return true, key
end

local function handle_action_packet(packet)
    if not packet then
        return
    end

    local actor_id = packet.Actor or packet.actor_id
    local actor_index = packet['Actor Index'] or packet.actor_index
    local source = mob_by_id_or_index(actor_id, actor_index)
    if not source then
        return
    end
    source = enrich_from_party(source)

    local party = party_ids()
    local category = action_category(packet)
    local special = special_action_categories[category] or false
    local target_count = packet.targets and #packet.targets or 0
    local now = os.clock()
    for _, action_target in ipairs(packet.targets or {}) do
        local target = mob_by_id_or_index(action_target.id, action_target.index)
        if target then
            target = enrich_from_party(target)
            local pair_key = tostring(source.id) .. '>' .. tostring(target.id)
            local spell_key = nil
            if category == 4 or category == 8 then
                spell_key = pair_key .. ':' .. tostring(action_spell_id(category, packet, action_target))
            end
            if spell_key and recent_spell_events[spell_key] and now - recent_spell_events[spell_key] <= duplicate_finish_window then
                -- Categories 4 and 8 can arrive in either order for spells; keep only newly affected AoE targets.
                action_debug_log('skip', packet, source, target, action_target, 'repeat_spell_target')
                target = nil
            elseif (category == 3 or category == 11) and recent_ability_starts[pair_key] and now - recent_ability_starts[pair_key] <= duplicate_finish_window then
                -- Category 3/11 can repeat the TP/ability ready target on resolve; keep only newly affected AoE targets.
                action_debug_log('skip', packet, source, target, action_target, 'repeat_ability_target')
                target = nil
            end
        end

        if target then
            local fan = target_count > 1 and (category == 8 or category == 11)
            local allowed, allow_reason = line_allowed(source, target, party, special, fan)
            if not allowed then
                if special or settings.action_debug then
                    action_debug_log('skip', packet, source, target, action_target, allow_reason)
                end
                target = nil
            end
        end

        if target then
            local kind, color = classify_line(source, target, party)
            local fan = target_count > 1 and (category == 8 or category == 11)
            color = scale_color_alpha(color, role_opacity_scale(line_source_role(source, party)))
            if special then
                kind = kind .. '_special'
            end
            if fan then
                kind = kind .. '_fan'
                color = scale_color_alpha(color, tonumber(settings.fan_opacity_scale) or defaults.fan_opacity_scale)
            end

            local drawn, reason = add_recent_line(source, target, kind, color, effective_timeout(), {always_draw = special, special_cooldown = special})
            if special or settings.action_debug then
                action_debug_log(drawn and 'draw' or 'skip', packet, source, target, action_target, reason or kind)
            end
            if category == 4 then
                recent_spell_starts[tostring(source.id) .. '>' .. tostring(target.id)] = now
            end
            if category == 4 or category == 8 then
                recent_spell_events[tostring(source.id) .. '>' .. tostring(target.id) .. ':' .. tostring(action_spell_id(category, packet, action_target))] = now
            elseif category == 7 then
                recent_ability_starts[tostring(source.id) .. '>' .. tostring(target.id)] = now
            end
        end
    end
end

local function describe_mob(mob)
    if not mob then
        return 'nil'
    end

    local target = mob.target_index and windower.ffxi.get_mob_by_index(mob.target_index) or nil
    return ('%s id=%s idx=%s hpp=%s dist=%.1f npc=%s race=%s model=%s models=%s size=%s scale=%s short=%s floating=%s status=%s spawn=%s valid=%s target_type=%s heading=%s anim=%s move=%s charmed=%s party=%s alliance=%s target_idx=%s target=%s/%s claim=%s')
        :format(tostring(mob.name), tostring(mob.id), tostring(mob.index), tostring(mob.hpp),
            mob_distance(mob), tostring(mob.is_npc), tostring(mob.race), tostring(primary_model(mob)),
            model_list(mob), tostring(mob.model_size), tostring(mob.model_scale), tostring(short_anchor(mob)), tostring(floating_anchor(mob)),
            tostring(mob.status), tostring(mob.spawn_type), tostring(mob.valid_target), tostring(mob.target_type),
            tostring(mob.heading), tostring(mob.animation_speed), tostring(mob.movement_speed), tostring(mob.charmed),
            tostring(mob.in_party), tostring(mob.in_alliance), tostring(mob.target_index),
            tostring(target and target.name), tostring(target and target.id), tostring(mob.claim_id))
end

append_runtime_log = function(line)
    local file = io.open(runtime_log_path, 'a')
    if not file then
        return
    end

    file:write(os.date('%Y-%m-%d %H:%M:%S') .. ' ' .. tostring(line) .. '\n')
    file:close()
end

local function debug_value(value)
    local value_type = type(value)
    if value_type == 'string' then
        local text = value:gsub('%s+', ' ')
        if #text > 80 then
            text = text:sub(1, 77) .. '...'
        end
        return text
    elseif value_type == 'number' or value_type == 'boolean' then
        return tostring(value)
    elseif value_type == 'table' then
        return 'table#' .. tostring(#value)
    elseif value == nil then
        return 'nil'
    end

    return value_type
end

local function debug_fields(value, limit)
    if type(value) ~= 'table' then
        return tostring(value)
    end

    local keys = {}
    for key in pairs(value) do
        keys[#keys + 1] = key
    end

    table.sort(keys, function(left, right)
        return tostring(left) < tostring(right)
    end)

    local fields = {}
    for index, key in ipairs(keys) do
        if index > limit then
            fields[#fields + 1] = '...'
            break
        end

        fields[#fields + 1] = tostring(key) .. '=' .. debug_value(value[key])
    end

    return table.concat(fields, ',')
end

action_debug_log = function(event, packet, source, target, action_target, reason)
    if not settings.action_debug then
        return
    end

    local category = action_category(packet)
    local param = packet and (packet.param or packet.Param or packet['Param']) or nil
    local message = packet and (packet.message or packet.Message or packet['Message']) or nil
    local target_count = packet and packet.targets and #packet.targets or 0
    local action_count = action_target and action_target.actions and #action_target.actions or 0
    local first_action = action_target and action_target.actions and action_target.actions[1] or {}
    local action_message = first_action.message or first_action.Message
    local action_param = first_action.param or first_action.Param

    append_runtime_log(('action_debug event=%s cat=%s param=%s msg=%s target_count=%s action_count=%s source=%s/%s/%s target=%s/%s/%s action_msg=%s action_param=%s reason=%s')
        :format(tostring(event), tostring(category), tostring(param), tostring(message), tostring(target_count), tostring(action_count),
            tostring(source and source.name), tostring(source and source.id), tostring(source and source.index),
            tostring(target and target.name), tostring(target and target.id), tostring(target and target.index),
            tostring(action_message), tostring(action_param), tostring(reason)))
    append_runtime_log(('action_debug_fields packet={%s} target_entry={%s} first_action={%s}')
        :format(debug_fields(packet, 28), debug_fields(action_target, 28), debug_fields(first_action, 28)))
end

local function write_inspect_log(reason)
    local file = io.open(inspect_path, 'a')
    if not file then
        warning('Could not write ' .. inspect_path)
        return false
    end

    file:write('---\n')
    file:write('time=' .. os.date('%Y-%m-%d %H:%M:%S') .. ' reason=' .. tostring(reason or 'manual') .. '\n')
    local recent_count = 0
    for _ in pairs(recent_lines) do
        recent_count = recent_count + 1
    end

    file:write(('enabled=%s range=%s debug=%s recent=%u visible=%u\n')
        :format(tostring(settings.enabled), tostring(settings.scan_range), tostring(settings.debug), recent_count, #last_lines))
    file:write('me=' .. describe_mob(windower.ffxi.get_mob_by_target('me')) .. '\n')
    file:write('current_target=' .. describe_mob(windower.ffxi.get_mob_by_target('t')) .. '\n')

    for index, line in ipairs(last_lines) do
        file:write(('line_%u=%s %s -> %s age=%.2f progress=%.2f\n')
            :format(index, tostring(line.kind), describe_mob(line.source), describe_mob(line.target),
                tonumber(line.age) or 0, tonumber(line.progress) or 0))
    end

    file:write('nearby=' .. tostring(#last_nearby) .. '\n')
    for index = 1, math.min(#last_nearby, 25) do
        file:write(('nearby_%u=%s\n'):format(index, describe_mob(last_nearby[index])))
    end

    file:close()
    return true
end

local function nearby_enemy_count(nearby)
    local count = 0
    local party = party_ids()
    for _, mob in ipairs(nearby or {}) do
        if is_visible_npc(mob) and not party_entity(mob, party) then
            count = count + 1
        end
    end

    return count
end

local function update_auto_inspect(nearby)
    if settings.auto_inspect ~= true then
        return
    end

    local info = windower.ffxi.get_info() or {}
    local zone = tonumber(info.zone) or 0
    if zone ~= auto_inspect_zone then
        auto_inspect_zone = zone
        auto_inspect_zone_written = false
    end

    local enemies = nearby_enemy_count(nearby)
    if enemies <= 0 then
        return
    end

    local now = os.clock()
    if not auto_inspect_zone_written then
        if write_inspect_log('auto_zone') then
            auto_inspect_last = now
            auto_inspect_zone_written = true
            log(('TargetLines auto inspect complete: auto_zone, %u nearby enemies.'):format(enemies))
        end
        return
    end

    local interval = math.max(30, tonumber(settings.auto_inspect_interval) or defaults.auto_inspect_interval)
    if now - auto_inspect_last >= interval then
        if write_inspect_log('auto_interval') then
            auto_inspect_last = now
            log(('TargetLines auto inspect complete: auto_interval, %u nearby enemies.'):format(enemies))
        end
    end
end

local function collect_claim_lines(party)
    local lines = {}
    local nearby = {}
    local range = tonumber(settings.scan_range) or defaults.scan_range
    local colors = active_colors()

    for _, mob in pairs(get_mobs()) do
        if is_visible_npc(mob) and mob_distance(mob) <= range then
            nearby[#nearby + 1] = mob
            if mob.claim_id and party[mob.claim_id] then
                local target = party_member_point_by_id(mob.claim_id) or player_point()
                lines[#lines + 1] = {
                    source = mob,
                    target = target,
                    kind = 'enemy',
                    color = colors.claim_enemy,
                    created = os.clock(),
                    timeout = tonumber(settings.claim_timeout) or defaults.claim_timeout,
                    uid = 0,
                    claim = true,
                }
            end
        end
    end

    table.sort(nearby, function(left, right)
        return mob_distance(left) < mob_distance(right)
    end)

    return lines, nearby
end

local function collect_lines()
    local lines = {}
    local now = os.clock()
    local party = party_ids()

    for key, line in pairs(recent_lines) do
        local age = now - (line.created or now)
        local timeout = tonumber(line.timeout) or defaults.action_timeout
        if age > timeout then
            recent_lines[key] = nil
        else
            lines[#lines + 1] = line
        end
    end

    local nearby = {}
    if settings.claim_fallback then
        local claim_lines = nil
        claim_lines, nearby = collect_claim_lines(party)
        for _, line in ipairs(claim_lines) do
            lines[#lines + 1] = line
        end
    else
        for _, mob in pairs(get_mobs()) do
            if is_visible_npc(mob) and mob_distance(mob) <= (tonumber(settings.scan_range) or defaults.scan_range) then
                nearby[#nearby + 1] = mob
            end
        end

        table.sort(nearby, function(left, right)
            return mob_distance(left) < mob_distance(right)
        end)
    end

    if regular_mode() == 'repeat' then
        local cooldown = tonumber(settings.pair_cooldown) or defaults.pair_cooldown
        for key, last_seen in pairs(seen_pairs) do
            if now - last_seen > cooldown then
                seen_pairs[key] = nil
            end
        end
    end

    local special_cooldown = tonumber(settings.special_cooldown) or defaults.special_cooldown
    for key, last_seen in pairs(seen_special_pairs) do
        if now - last_seen > special_cooldown then
            seen_special_pairs[key] = nil
        end
    end

    for key, last_seen in pairs(recent_spell_starts) do
        if now - last_seen > duplicate_finish_window then
            recent_spell_starts[key] = nil
        end
    end

    for key, last_seen in pairs(recent_spell_events) do
        if now - last_seen > duplicate_finish_window then
            recent_spell_events[key] = nil
        end
    end

    for key, last_seen in pairs(recent_ability_starts) do
        if now - last_seen > duplicate_finish_window then
            recent_ability_starts[key] = nil
        end
    end

    table.sort(lines, function(left, right)
        return (left.created or 0) > (right.created or 0)
    end)

    return lines, nearby
end

local function point_json(mob)
    return ('{"id":%u,"index":%u,"name":%s,"x":%.4f,"y":%.4f,"z":%.4f,"hpp":%u,"facing":%.6f,"npc":%s,"race":%u,"model":%u,"model_size":%.3f,"model_scale":%.3f,"short_anchor":%s,"floating_anchor":%s}')
        :format(tonumber(mob.id) or 0, tonumber(mob.index) or 0, json_string(mob.name),
            tonumber(mob.x) or 0, tonumber(mob.y) or 0, tonumber(mob.z) or 0,
            tonumber(mob.hpp) or 0, tonumber(mob.facing) or 0,
            mob.is_npc and 'true' or 'false',
            tonumber(mob.race) or 0, primary_model(mob),
            tonumber(mob.model_size) or 0, tonumber(mob.model_scale) or 1,
            short_anchor(mob) and 'true' or 'false',
            floating_anchor(mob) and 'true' or 'false')
end

update_config_box = function()
    if not config_visible then
        config_box:hide()
        return
    end

    config_box:pos(settings.display.pos.x, settings.display.pos.y)
    local rows = {
        ('%-' .. tostring(config_title_width) .. 's [x]'):format('TargetLines Settings'),
        '',
    }

    for _, row in ipairs(config_rows) do
        local master_disabled = settings.enabled == false and row.name ~= 'enabled'
        local label = setting_label_column(row)
        if row.type == 'toggle' then
            local value = settings[row.name] ~= false and ' ON ' or ' OFF'
            local row_disabled = master_disabled or settings[row.name] == false
            rows[#rows + 1] = ('%s[%s]'):format(row_disabled and disabled_value(label) or label, row_disabled and disabled_value(value) or value)
        elseif row.type == 'choice' then
            local value = ('[<] %s [>]'):format(centered_text(preset_label(row.name), 18))
            rows[#rows + 1] = ('%s%s'):format(master_disabled and disabled_value(label) or label, master_disabled and disabled_value(value) or value)
        end
    end

    rows[#rows + 1] = ''
    rows[#rows + 1] = 'Click [<] [>] or [ON/OFF]'

    config_box.current_string = table.concat(rows, '\n')
    config_box:show()
end

local function encode_state(lines)
    local info = windower.ffxi.get_info() or {}
    local observer = player_point(windower.ffxi.get_player())
    local parts = {
        ('"time":%.3f'):format(os.clock()),
        ',',
        ('"zone":%u'):format(tonumber(info.zone) or 0),
        ',',
        ('"settings":{"opacity":%.3f,"timeout":%.3f,"width":%.3f,"glow":%.3f,"sourceheight":%.3f,"targetheight":%.3f}'):format(
            clamp(defaults.opacity * slider_scale('opacity_scale'), 0, 1),
            effective_timeout(),
            slider_scale('width_scale'),
            slider_scale('glow_scale'),
            effective_source_height(),
            effective_target_height()),
        ',',
        ('"boneprobe":%s'):format(os.clock() < boneprobe_until and 'true' or 'false'),
        ',',
        ('"observer":%s'):format(point_json(observer)),
        ',',
        '"lines":[',
    }

    for index, line in ipairs(lines) do
        if index > 1 then
            parts[#parts + 1] = ','
        end

        local color = tonumber(line.color) or 0xEFFFFFFF
        local timeout = tonumber(line.timeout) or effective_timeout()
        parts[#parts + 1] = ('{"uid":%u,"source":%s,"target":%s,"kind":%s,"color":%u,"timeout":%.3f}')
            :format(tonumber(line.uid) or 0, point_json(line.source), point_json(line.target), json_string(line.kind),
                color, timeout)
    end

    parts[#parts + 1] = ']'
    parts[#parts + 1] = ',"probe_lines":['
    for index, line in ipairs(probe_lines) do
        if index > 1 then
            parts[#parts + 1] = ','
        end

        local color = tonumber(line.color) or 0xEFFFFFFF
        local timeout = tonumber(line.timeout) or effective_timeout()
        parts[#parts + 1] = ('{"uid":%u,"source":%s,"target":%s,"kind":%s,"color":%u,"timeout":%.3f}')
            :format(tonumber(line.uid) or 0, point_json(line.source), point_json(line.target), json_string(line.kind),
                color, timeout)
    end

    parts[#parts + 1] = ']}'
    return '{' .. table.concat(parts) .. '\n'
end

local function write_state(lines)
    local state = encode_state(lines)
    if state == last_signature then
        return
    end

    ensure_state_dir()
    local file = io.open(state_path, 'w')
    if not file then
        warning('Could not write ' .. state_path)
        return
    end

    file:write(state)
    file:close()
    last_signature = state
end

local function update_debug(lines)
    if not settings.debug then
        box:hide()
        return
    end

    local rows = {'TargetLines lines: ' .. tostring(#lines)}
    for index = 1, math.min(#lines, 8) do
        local line = lines[index]
        rows[#rows + 1] = ('%s %s -> %s %.2f')
            :format(line.kind or '?', line.source.name or '?', line.target.name or '?', line.progress or 0)
    end

    box.current_string = table.concat(rows, '\n')
    box:show()
end

windower.register_event('incoming chunk', function(id, data)
    if not settings.enabled or id ~= 0x028 or type(windower.packets.parse_action) ~= 'function' then
        return
    end

    local ok, packet = pcall(windower.packets.parse_action, data)
    if ok then
        handle_action_packet(packet)
    end
end)

windower.register_event('prerender', function()
    if not settings.enabled then
        box:hide()
        return
    end

    local now = os.clock()
    if now - last_write < (tonumber(settings.write_interval) or defaults.write_interval) then
        return
    end

    last_write = now
    local lines, nearby = collect_lines()
    last_lines = lines
    last_nearby = nearby
    write_state(lines)
    update_auto_inspect(nearby)
    update_debug(lines)
    update_config_box()
end)

windower.register_event('load', function()
    ensure_state_dir()
    append_runtime_log('loaded enabled=' .. tostring(settings.enabled) .. ' debug=' .. tostring(settings.debug))
end)

windower.register_event('addon command', function(command, ...)
    command = command and command:lower() or 'help'
    local args = {...}

    if command == 'on' then
        settings.enabled = true
        config.save(settings)
        log('TargetLines enabled.')
    elseif command == 'off' then
        settings.enabled = false
        config.save(settings)
        box:hide()
        log('TargetLines disabled.')
    elseif command == 'debug' then
        local value = args[1] and args[1]:lower() or nil
        if value == 'on' or value == '1' or value == 'true' then
            settings.debug = true
        elseif value == 'off' or value == '0' or value == 'false' then
            settings.debug = false
        else
            settings.debug = not settings.debug
        end
        config.save(settings)
        append_runtime_log('debug=' .. tostring(settings.debug))
    elseif command == 'actiondebug' or command == 'adebug' then
        local value = args[1] and args[1]:lower() or nil
        if value == 'on' or value == '1' or value == 'true' then
            settings.action_debug = true
        elseif value == 'off' or value == '0' or value == 'false' then
            settings.action_debug = false
        else
            settings.action_debug = not settings.action_debug
        end
        config.save(settings)
        append_runtime_log('action_debug=' .. tostring(settings.action_debug))
        log('TargetLines action debug ' .. (settings.action_debug and 'enabled.' or 'disabled.') .. ' Output: ' .. runtime_log_path)
    elseif command == 'show' then
        settings.enabled = true
        settings.debug = true
        settings.display.pos.x = 160
        settings.display.pos.y = 220
        config.save(settings)
        box:pos(settings.display.pos.x, settings.display.pos.y)
        box.current_string = 'TargetLines lines: ' .. tostring(#last_lines)
        box:show()
        append_runtime_log('show command forced box visible')
    elseif command == 'config' or command == 'settings' then
        config_visible = not config_visible
        update_config_box()
        log('TargetLines config ' .. (config_visible and 'shown.' or 'hidden.'))
    elseif command == 'playerlines' or command == 'player' then
        log('TargetLines player lines ' .. (set_boolean_from_arg('show_player_lines', args[1]) and 'enabled.' or 'disabled.'))
    elseif command == 'partylines' or command == 'party' or command == 'trustlines' or command == 'trusts' then
        log('TargetLines party/trust lines ' .. (set_boolean_from_arg('show_party_lines', args[1]) and 'enabled.' or 'disabled.'))
    elseif command == 'petlines' or command == 'pets' then
        log('TargetLines pet lines ' .. (set_boolean_from_arg('show_pet_lines', args[1]) and 'enabled.' or 'disabled.'))
    elseif command == 'enemylines' or command == 'enemy' then
        log('TargetLines enemy lines ' .. (set_boolean_from_arg('show_enemy_lines', args[1]) and 'enabled.' or 'disabled.'))
    elseif command == 'otherpartylines' or command == 'otherparty' or command == 'others' then
        log('TargetLines other party lines ' .. (set_boolean_from_arg('show_other_party_lines', args[1]) and 'enabled.' or 'disabled.'))
    elseif command == 'speciallines' or command == 'specials' then
        log('TargetLines special action lines ' .. (set_boolean_from_arg('show_special_lines', args[1]) and 'enabled.' or 'disabled.'))
    elseif command == 'fanlines' or command == 'fan' or command == 'aoe' then
        log('TargetLines AoE fan lines ' .. (set_boolean_from_arg('show_fan_lines', args[1]) and 'enabled.' or 'disabled.'))
    elseif command == 'colorblind' or command == 'colourblind' or command == 'cbmode' then
        log('TargetLines color blind mode ' .. (set_boolean_from_arg('color_blind_mode', args[1]) and 'enabled.' or 'disabled.'))
    elseif command == 'playeropacity' or command == 'popacity' then
        adjust_opacity_command('playeropacity', 'player_opacity_scale', 'player opacity', args[1] and args[1]:lower() or nil)
    elseif command == 'allyopacity' or command == 'aopacity' then
        adjust_opacity_command('allyopacity', 'ally_opacity_scale', 'ally opacity', args[1] and args[1]:lower() or nil)
    elseif command == 'enemyopacity' or command == 'eopacity' then
        adjust_opacity_command('enemyopacity', 'enemy_opacity_scale', 'enemy opacity', args[1] and args[1]:lower() or nil)
    elseif command == 'opacity' then
        local value = args[1] and args[1]:lower() or nil
        if value == '+' then
            adjust_slider('opacity_scale', 0.01)
            log(('TargetLines opacity scale set to %d%%.'):format(math.floor(slider_scale('opacity_scale') * 100 + 0.5)))
        elseif value == '-' then
            adjust_slider('opacity_scale', -0.01)
            log(('TargetLines opacity scale set to %d%%.'):format(math.floor(slider_scale('opacity_scale') * 100 + 0.5)))
        else
            local opacity = tonumber(args[1])
            if not (opacity and opacity >= 0 and opacity <= 1) then
                warning('Usage: //tl opacity <0-1>|+|-')
                return
            end
            settings.opacity = opacity
            settings.opacity_scale = clamp(opacity / defaults.opacity, 0.5, 2.0)
            config.save(settings)
            last_signature = ''
            update_config_box()
            log(('TargetLines opacity set to %.2f.'):format(opacity))
        end
    elseif command == 'fade' then
        local value = args[1] and args[1]:lower() or nil
        if value == '+' then
            adjust_slider('fade_scale', 0.01)
        elseif value == '-' then
            adjust_slider('fade_scale', -0.01)
        else
            warning('Usage: //tl fade +/-')
            return
        end
        log(('TargetLines fade scale set to %d%%.'):format(math.floor(slider_scale('fade_scale') * 100 + 0.5)))
    elseif command == 'width' then
        local value = args[1] and args[1]:lower() or nil
        if value == '+' then
            adjust_slider('width_scale', 0.01)
        elseif value == '-' then
            adjust_slider('width_scale', -0.01)
        else
            warning('Usage: //tl width +/-')
            return
        end
        log(('TargetLines width scale set to %d%%.'):format(math.floor(slider_scale('width_scale') * 100 + 0.5)))
    elseif command == 'glow' then
        local value = args[1] and args[1]:lower() or nil
        if value == '+' then
            adjust_slider('glow_scale', 0.01)
        elseif value == '-' then
            adjust_slider('glow_scale', -0.01)
        else
            warning('Usage: //tl glow +/-')
            return
        end
        log(('TargetLines glow scale set to %d%%.'):format(math.floor(slider_scale('glow_scale') * 100 + 0.5)))
    elseif command == 'fanopacity' or command == 'fanopacityscale' then
        local value = args[1] and args[1]:lower() or nil
        local current = tonumber(settings.fan_opacity_scale) or defaults.fan_opacity_scale
        if value == '+' then
            settings.fan_opacity_scale = clamp(current + 0.01, 0.1, 1.0)
        elseif value == '-' then
            settings.fan_opacity_scale = clamp(current - 0.01, 0.1, 1.0)
        else
            local opacity = tonumber(args[1])
            if not (opacity and opacity >= 0.1 and opacity <= 1) then
                warning('Usage: //tl fanopacity <0.1-1>|+|-')
                return
            end
            settings.fan_opacity_scale = opacity
        end
        config.save(settings)
        last_signature = ''
        update_config_box()
        log(('TargetLines AoE fan opacity set to %d%%.'):format(math.floor(settings.fan_opacity_scale * 100 + 0.5)))
    elseif command == 'sourceheight' or command == 'sourceht' then
        local value = args[1] and args[1]:lower() or nil
        if value == '+' then
            adjust_slider('source_height_scale', 0.01)
        elseif value == '-' then
            adjust_slider('source_height_scale', -0.01)
        else
            warning('Usage: //tl sourceheight +/-')
            return
        end
        log(('TargetLines source height scale set to %d%%.'):format(math.floor(slider_scale('source_height_scale') * 100 + 0.5)))
    elseif command == 'targetheight' or command == 'targetht' then
        local value = args[1] and args[1]:lower() or nil
        if value == '+' then
            adjust_slider('target_height_scale', 0.01)
        elseif value == '-' then
            adjust_slider('target_height_scale', -0.01)
        else
            warning('Usage: //tl targetheight +/-')
            return
        end
        log(('TargetLines target height scale set to %d%%.'):format(math.floor(slider_scale('target_height_scale') * 100 + 0.5)))
    elseif command == 'range' then
        local range = tonumber(args[1])
        if range and range > 0 then
            settings.scan_range = range
            config.save(settings)
            log(('TargetLines range set to %.1f.'):format(range))
        else
            warning('Usage: //tl range <yalms>')
        end
    elseif command == 'timeout' then
        local timeout = tonumber(args[1])
        if timeout and timeout > 0 then
            settings.action_timeout = timeout
            settings.fade_scale = clamp(timeout / defaults.action_timeout, 0.5, 2.0)
            config.save(settings)
            last_signature = ''
            update_config_box()
            log(('TargetLines timeout set to %.1f seconds.'):format(timeout))
        else
            warning('Usage: //tl timeout <seconds>')
        end
    elseif command == 'interval' then
        local interval = tonumber(args[1])
        if interval and interval >= 0.008 then
            settings.write_interval = interval
            config.save(settings)
            update_config_box()
            log(('TargetLines update interval set to %.3f seconds.'):format(interval))
        else
            warning('Usage: //tl interval <seconds>, e.g. //tl interval 0.016')
        end
    elseif command == 'cooldown' then
        local cooldown = tonumber(args[1])
        if cooldown and cooldown >= 0 then
            settings.pair_cooldown = cooldown
            config.save(settings)
            update_config_box()
            log(('TargetLines pair cooldown set to %.1f seconds.'):format(cooldown))
        else
            warning('Usage: //tl cooldown <seconds>')
        end
    elseif command == 'specialcooldown' or command == 'specialcd' then
        local cooldown = tonumber(args[1])
        if cooldown and cooldown >= 0 then
            settings.special_cooldown = cooldown
            seen_special_pairs = {}
            config.save(settings)
            update_config_box()
            log(('TargetLines special cooldown set to %.1f seconds.'):format(cooldown))
        else
            warning('Usage: //tl specialcooldown <seconds>')
        end
    elseif command == 'regular' then
        local value = args[1] and args[1]:lower() or nil
        if value == 'once' or value == 'first' then
            set_regular_mode('first')
            log('TargetLines regular attacks set to once per source-target pair.')
        elseif value == 'cooldown' or value == 'repeat' or value == 'delay' then
            set_regular_mode('repeat')
            log('TargetLines regular attacks set to Repeat After Delay.')
        elseif value == 'off' or value == 'none' then
            set_regular_mode('off')
            log('TargetLines regular attacks disabled.')
        else
            warning('Usage: //tl regular first|repeat|off')
            return
        end
    elseif command == 'claim' then
        local value = args[1] and args[1]:lower() or nil
        if value == 'on' or value == '1' or value == 'true' then
            settings.claim_fallback = true
        elseif value == 'off' or value == '0' or value == 'false' then
            settings.claim_fallback = false
        else
            settings.claim_fallback = not settings.claim_fallback
        end

        config.save(settings)
        update_config_box()
        log('TargetLines claim fallback ' .. (settings.claim_fallback and 'enabled.' or 'disabled.'))
    elseif command == 'autoinspect' or command == 'inspectauto' then
        local value = args[1] and args[1]:lower() or nil
        if value == 'interval' then
            local interval = tonumber(args[2])
            if interval and interval >= 30 then
                settings.auto_inspect_interval = interval
                config.save(settings)
                log(('TargetLines auto inspect interval set to %.0f seconds.'):format(interval))
            else
                warning('Usage: //tl autoinspect interval <seconds>, minimum 30')
            end
            return
        elseif value == 'on' or value == '1' or value == 'true' then
            settings.auto_inspect = true
        elseif value == 'off' or value == '0' or value == 'false' then
            settings.auto_inspect = false
        else
            settings.auto_inspect = not settings.auto_inspect
        end

        auto_inspect_last = 0
        auto_inspect_zone = nil
        auto_inspect_zone_written = false
        config.save(settings)
        log(('TargetLines auto inspect %s. Interval: %.0f seconds.'):format(
            settings.auto_inspect and 'enabled' or 'disabled',
            tonumber(settings.auto_inspect_interval) or defaults.auto_inspect_interval))
    elseif command == 'clear' then
        recent_lines = {}
        seen_pairs = {}
        seen_special_pairs = {}
        recent_spell_starts = {}
        recent_spell_events = {}
        recent_ability_starts = {}
        probe_lines = {}
        last_signature = ''
        write_state({})
        log('TargetLines lines cleared.')
    elseif command == 'boneprobe' or command == 'anchorprobe' then
        local lines = (#last_lines > 0) and last_lines or probe_lines
        if not lines or #lines == 0 then
            warning('No recent TargetLines line is available to probe.')
            return
        end

        boneprobe_until = os.clock() + 1.0
        last_signature = ''
        write_state({lines[1]})
        append_runtime_log('boneprobe flag written for latest line')
        log('TargetLines bone probe requested for latest line.')
    elseif command == 'status' then
        log(('enabled=%s debug=%s action_debug=%s player=%s party=%s pet=%s enemy=%s other_party=%s special=%s fan=%s color_blind=%s range=%s global_opacity=%s player_opacity=%s ally_opacity=%s enemy_opacity=%s fan_opacity=%s fade=%s width=%s glow=%s source_height=%s target_height=%s interval=%s regular=%s repeat_delay=%s special_cooldown=%s claim=%s auto_inspect=%s auto_interval=%s state=%s lines=%s nearby=%s')
            :format(tostring(settings.enabled), tostring(settings.debug), tostring(settings.action_debug),
                tostring(settings.show_player_lines ~= false), tostring(settings.show_party_lines ~= false),
                tostring(settings.show_pet_lines ~= false), tostring(settings.show_enemy_lines ~= false),
                tostring(settings.show_other_party_lines ~= false),
                tostring(settings.show_special_lines ~= false), tostring(settings.show_fan_lines ~= false),
                tostring(settings.color_blind_mode == true),
                tostring(settings.scan_range), tostring(defaults.opacity * slider_scale('opacity_scale')),
                tostring(slider_scale('player_opacity_scale')), tostring(slider_scale('ally_opacity_scale')),
                tostring(slider_scale('enemy_opacity_scale')),
                tostring(tonumber(settings.fan_opacity_scale) or defaults.fan_opacity_scale),
                tostring(effective_timeout()),
                tostring(slider_scale('width_scale')), tostring(slider_scale('glow_scale')),
                tostring(effective_source_height()), tostring(effective_target_height()),
                tostring(settings.write_interval), regular_mode(),
                tostring(settings.pair_cooldown), tostring(settings.special_cooldown), tostring(settings.claim_fallback),
                tostring(settings.auto_inspect == true), tostring(tonumber(settings.auto_inspect_interval) or defaults.auto_inspect_interval),
                state_path, tostring(#last_lines), tostring(#last_nearby)))
        append_runtime_log(('status enabled=%s debug=%s action_debug=%s player=%s party=%s pet=%s enemy=%s other_party=%s special=%s fan=%s color_blind=%s range=%s global_opacity=%s player_opacity=%s ally_opacity=%s enemy_opacity=%s fan_opacity=%s fade=%s width=%s glow=%s source_height=%s target_height=%s interval=%s regular=%s repeat_delay=%s special_cooldown=%s claim=%s auto_inspect=%s auto_interval=%s lines=%s nearby=%s')
            :format(tostring(settings.enabled), tostring(settings.debug), tostring(settings.action_debug),
                tostring(settings.show_player_lines ~= false), tostring(settings.show_party_lines ~= false),
                tostring(settings.show_pet_lines ~= false), tostring(settings.show_enemy_lines ~= false),
                tostring(settings.show_other_party_lines ~= false),
                tostring(settings.show_special_lines ~= false), tostring(settings.show_fan_lines ~= false),
                tostring(settings.color_blind_mode == true),
                tostring(settings.scan_range), tostring(defaults.opacity * slider_scale('opacity_scale')),
                tostring(slider_scale('player_opacity_scale')), tostring(slider_scale('ally_opacity_scale')),
                tostring(slider_scale('enemy_opacity_scale')),
                tostring(tonumber(settings.fan_opacity_scale) or defaults.fan_opacity_scale),
                tostring(effective_timeout()),
                tostring(slider_scale('width_scale')), tostring(slider_scale('glow_scale')),
                tostring(effective_source_height()), tostring(effective_target_height()),
                tostring(settings.write_interval), regular_mode(),
                tostring(settings.pair_cooldown), tostring(settings.special_cooldown), tostring(settings.claim_fallback),
                tostring(settings.auto_inspect == true), tostring(tonumber(settings.auto_inspect_interval) or defaults.auto_inspect_interval),
                tostring(#last_lines), tostring(#last_nearby)))
    elseif command == 'inspect' or command == 'i' then
        if write_inspect_log('manual') then
            log('Inspect snapshot written: ' .. inspect_path)
        end
    elseif native_probe_commands[command] then
        local native_command = command
        if #args > 0 then
            native_command = native_command .. ' ' .. table.concat(args, ' ')
        end
        windower.send_command('targetlines ' .. native_command)
        append_runtime_log('native command forwarded via addon alias: ' .. native_command)
        log('TargetLines native command forwarded: ' .. native_command)
    else
        log('Commands: //tl on | off | config | settings | playerlines [on|off] | partylines [on|off] | petlines [on|off] | enemylines [on|off] | otherpartylines [on|off] | speciallines [on|off] | fanlines [on|off] | colorblind [on|off] | regular first|repeat|off | playeropacity +/- | allyopacity +/- | enemyopacity +/- | opacity +/- | fade +/- | width +/- | glow +/- | fanopacity +/- | debug [on|off] | actiondebug [on|off] | sourceheight +/- | targetheight +/- | timeout <sec> | range <yalms> | interval <sec> | cooldown <sec> | specialcooldown <sec> | claim [on|off] | autoinspect [on|off] | autoinspect interval <sec> | boneprobe | luamobprobe | dynamicbone <auto|off|0-255> | clear | status | inspect')
    end
end)

windower.register_event('mouse', function(type, x, y, delta, blocked)
    return handle_mouse(type, x, y)
end)

windower.register_event('unload', function()
    box:hide()
    config_box:hide()
end)
