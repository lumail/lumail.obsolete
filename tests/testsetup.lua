-- Set an output log file
local io = require('io')
local os = require('os')
io.output(os.getenv("OUTFILE"))
