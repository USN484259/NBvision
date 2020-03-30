local function judge(hwnd,op,arg)
	if op == NBvision.HCBT_CREATEWND then
		print(NBvision.pid,hwnd,arg.class,arg.title)
		if type(arg.class) ~= "string" then
			arg.class = ""
		end
		if arg.class == "SDL_app" then
			print("killed",NBvision.pid,"SDL_app")
			--NBvision.terminate()
			return false
		end
		--[[
		if string.find(NBvision.process,"Steam.exe") ~= nil and string.find(arg.title,"Steam - ") ~= nil then
			print("killed",NBvision.pid,"steam news")
			return false
		end
		--]]
	end
	return true
end

print(NBvision.pid,NBvision.process)


return judge