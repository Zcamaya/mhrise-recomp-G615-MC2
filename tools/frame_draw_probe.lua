-- Stage with adb_lua.py stage-live. Pixel (120,120) should be RGB(11,215,99)
-- with the MOD UI closed. Remove the script and reload after capturing it.
local reported = false
re.on_frame(function()
    local size = imgui.get_display_size()
    assert(size.x > 0 and size.y > 0)
    draw.filled_rect(100, 100, 40, 40, 0xff63d70b)
    imgui.set_next_window_pos({180, 110})
    imgui.push_style_var(3, 7.5)
    if imgui.begin_window("Lua frame probe", nil, 64) then
        imgui.push_id(1)
        imgui.text("on_frame")
        imgui.pop_id()
        imgui.pop_style_var(1)
        imgui.end_window()
    else
        imgui.pop_style_var(1)
    end
    if not reported then
        log.info(("Lua frame probe: %dx%d menu=%s"):format(
            size.x, size.y, tostring(reframework:is_drawing_ui())))
        reported = true
    end
end)
