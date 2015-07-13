local M = {}

function M.testvar(setter, newval)
    local val = setter()
    io.write(tostring(val)..'\n')
    setter(newval)
    val = setter()
    io.write(val..'\n')
end

return M