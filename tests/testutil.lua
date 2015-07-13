local M = {}

function M.testvar(setter, origval, newval)
    local val = setter()
    io.write(val..'\n')
    setter(newval)
    val = setter()
    io.write(val..'\n')
end

return M