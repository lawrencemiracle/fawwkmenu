script_name('fawwk example')
script_author('fawwk')
script_version('1.0.0')

-- a throwaway mod that shows every control type. delete it once you've
-- copied what you need into your own script.

local function fawwkSdk()
    local ok, sdk = pcall(function()
        local ffi = require 'ffi'
        pcall(ffi.cdef, 'void* GetModuleHandleA(const char*); void* GetProcAddress(void*, const char*);')
        local mod = ffi.C.GetModuleHandleA('fawwkmenu.asi')
        local addr = mod ~= nil and ffi.C.GetProcAddress(mod, 'fawwk_sdk') or nil
        if addr == nil then return nil end
        return assert(loadstring(ffi.string(ffi.cast('const char*(*)(void)', addr)()), '=fawwk sdk'))()
    end)
    if ok and sdk then package.loaded.fawwk = sdk return sdk end
    local ok2, file = pcall(require, 'fawwk')   -- a copy on disk still works, for old plugin versions
    return ok2 and file or nil
end
local fawwk = fawwkSdk()
if not fawwk then return end   -- the menu is not installed, nothing to do


local cfg = fawwk.mod {
  id = 'fawwk.example',
  name = 'Example Mod',
  name_pt = 'Mod de Exemplo',   -- optional translations: name_pt, label_pt, desc_pt ...
  category = 'Demo',
  version = '1.0',
  author = 'fawwk',
  description = 'Every control the menu knows about, wired to a few harmless things.',
}

cfg:page('General')
cfg:toggle('enabled', 'Enabled', true, { desc = 'Master switch for this example.' })
cfg:slider('speed', 'Speed', 1.0, 0.1, 5.0, { step = 0.1, fmt = '%.1fx' })
cfg:int('count', 'How many', 5, 1, 20)
cfg:combo('mode', 'Mode', 1, { 'Calm', 'Normal', 'Wild' })
cfg:key('hotkey', 'Hotkey', 0x4E, { desc = 'Press it in game to see a message.' })

cfg:page('Extras')
cfg:group('Look')
cfg:color('tint', 'Tint', 0xFF3399FF)
cfg:color('glow', 'Glow (with alpha)', 0x8000FFAA, { alpha = true })
cfg:group('Other')
cfg:text('tag', 'Label text', 'hello', { maxlen = 24 })
cfg:toggle('advanced', 'Show advanced', false)
cfg:slider('jitter', 'Jitter', 0.2, 0, 1, { step = 0.01, show = 'advanced', desc = 'Only visible when Show advanced is on.' })
cfg:note('Buttons run a function in your script when pressed in the menu.')
cfg:button('say', 'Say something', function()
  printStringNow('hi from the example, mode: ' .. tostring(cfg:option('mode')), 2000)
end, { text = 'Say it' })

-- react to a change right when it happens
cfg:on('enabled', function(on)
  printStringNow(on and 'example on' or 'example off', 1500)
end)

function main()
  while true do
    wait(0)
    if cfg.v.enabled and cfg:pressed('hotkey') then
      printStringNow(('~y~%s~w~ x%d at %.1f'):format(cfg.v.tag, cfg.v.count, cfg.v.speed), 2000)
    end
  end
end
