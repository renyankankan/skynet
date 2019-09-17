local args = {}
-- 使用空格分隔参数
for word in string.gmatch(..., "%S+") do
	table.insert(args, word)
end

-- 第一个参数是服务名字
SERVICE_NAME = args[1]

local main, pattern

local err = {}
-- ';'分隔LUA_SERVICE
for pat in string.gmatch(LUA_SERVICE, "([^;]+);*") do
	-- SERVICE_NAME替换'?'形成文件路径
	local filename = string.gsub(pat, "?", SERVICE_NAME)
	-- 加载文件
	local f, msg = loadfile(filename)
	if not f then
		table.insert(err, msg)
	else
		pattern = pat
		main = f
		break
	end
end

if not main then
	error(table.concat(err, "\n"))
end

LUA_SERVICE = nil
package.path , LUA_PATH = LUA_PATH
package.cpath , LUA_CPATH = LUA_CPATH

local service_path = string.match(pattern, "(.*/)[^/?]+$")

if service_path then
	service_path = string.gsub(service_path, "?", args[1])
	package.path = service_path .. "?.lua;" .. package.path
	SERVICE_PATH = service_path
else
	local p = string.match(pattern, "(.*/).+$")
	SERVICE_PATH = p
end

if LUA_PRELOAD then
	local f = assert(loadfile(LUA_PRELOAD))
	f(table.unpack(args))
	LUA_PRELOAD = nil
end

-- 返回args第2个之后的的参数
main(select(2, table.unpack(args)))
