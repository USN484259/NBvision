#include <Windows.h>
#include <psapi.h>
#include <cstdio>
#include <stdexcept>
#include <mutex>
#include "shared_stream.hpp"

#include "lua.h"
#include "lualib.h"
#pragma comment(lib,"lua_vm.lib")




#pragma section(".shared",read,write,shared)

__declspec(allocate(".shared"))
USNLIB::shared_stream<0x0800> stream;

__declspec(allocate(".shared"))
size_t script_length;

__declspec(allocate(".shared"))
alignas(0x1000)
char script[0x1000];

struct {
	enum : unsigned {SIZE = 0x200};
	unsigned count;
	char buffer[SIZE];

	void append(const char* str) {
		while (*str && count < SIZE) {
			buffer[count++] = *str++;
		}
	}
}lua_stream = { 0 };

extern void stream_write(const char* str) {
	lua_stream.append(str);
}

extern void stream_flush(void) {
	stream.write(lua_stream.buffer, lua_stream.count);
	lua_stream.count = 0;
}


class lua_vm {
	HANDLE heap;
	lua_State* ls;
	DWORD pid;

	void report(const char* msg) {
		char str[0x20];
		snprintf(str,0x20, "%X\t", pid);
		stream_write(str);
		stream_write(msg);
		stream_write("\n");
		stream_flush();
	}

	static void* allocator(void* ud, void* ptr, size_t, size_t nsize) {
		HANDLE heap = (HANDLE)ud;

		if (NULL == heap)
			return nullptr;

		if (0 == nsize) {
			HeapFree(heap, 0, ptr);
			return nullptr;
		}
		if (nullptr == ptr) {
			return HeapAlloc(heap, 0, nsize);
		}
		return HeapReAlloc(heap, 0, ptr, nsize);
	}

	static int panic(lua_State* ls) {
		throw std::runtime_error("lua_panic");
	}

	struct reader_info {
		const char* buf;
		size_t len;
	};

	static const char* reader(lua_State*, void* ud, size_t* size) {
		reader_info* block = (reader_info*)ud;
		const char* res = block->buf;
		*size = block->len;

		block->buf = nullptr;
		block->len = 0;

		return res;
	}

	static int lfunc_wnd_class(lua_State* ls) {
		if (!lua_islightuserdata(ls, -1)) {
			lua_pushstring(ls, "lfunc_wnd_class expect HWND");
			lua_error(ls);
		}
		HWND hwnd = (HWND)lua_touserdata(ls, -1);
		char buffer[0x400];
		if (0 == GetClassNameA(hwnd, buffer, 0x400))
			return 0;
		lua_pushstring(ls,buffer);
		return 1;
	}

	static int lfunc_wnd_title(lua_State* ls) {
		if (!lua_islightuserdata(ls, -1)) {
			lua_pushstring(ls, "lfunc_wnd_title expect HWND");
			lua_error(ls);
		}
		HWND hwnd = (HWND)lua_touserdata(ls, -1);
		char buffer[0x400];
		if (0 == GetWindowTextA(hwnd, buffer, 0x400))
			return 0;
		lua_pushstring(ls, buffer);
		return 1;
	}

	static int lfunc_wnd_proc(lua_State* ls) {
		if (!lua_islightuserdata(ls, -1)) {
			lua_pushstring(ls, "lfunc_wnd_proc expect HWND");
			lua_error(ls);
		}
		int ret = 0;
		DWORD pid = 0;
		HANDLE hProc = NULL;
		do {
			HWND hwnd = (HWND)lua_touserdata(ls, -1);
			if (!hwnd)
				break;

			GetWindowThreadProcessId(hwnd, &pid);
			if (0 == pid)
				break;
			lua_pushinteger(ls, pid);
			ret = 1;

			hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

			char buffer[0x400];
			if (NULL == hProc || 0 == GetProcessImageFileNameA(hProc, buffer, 0x400))
				break;

			lua_pushstring(ls, buffer);
			ret = 2;
		} while (false);

		if (hProc)
			CloseHandle(hProc);

		return ret;
	}

	static void ltab_make_rect(lua_State* ls,const RECT* rect) {
		lua_createtable(ls, 0, 4);

		lua_pushstring(ls, "left");
		lua_pushinteger(ls, rect->left);
		lua_settable(ls, -3);

		lua_pushstring(ls, "right");
		lua_pushinteger(ls, rect->right);
		lua_settable(ls, -3);

		lua_pushstring(ls, "top");
		lua_pushinteger(ls, rect->top);
		lua_settable(ls, -3);

		lua_pushstring(ls, "bottom");
		lua_pushinteger(ls, rect->bottom);
		lua_settable(ls, -3);
	}

	static int lfunc_wnd_rect(lua_State* ls) {
		if (!lua_islightuserdata(ls, -1)) {
			lua_pushstring(ls, "lfunc_wnd_rect expect HWND");
			lua_error(ls);
		}
		HWND hwnd = (HWND)lua_touserdata(ls, -1);
		RECT rect = { 0 };
		if (!GetWindowRect(hwnd, &rect))
			return 0;

		ltab_make_rect(ls, &rect);
		return 1;
	}

	void open_lib_NBvision(void) {
		lua_createtable(ls, 0, 6);
		
		lua_pushstring(ls, "pid");
		lua_pushinteger(ls, pid);
		lua_settable(ls, -3);

		{
			char buffer[0x400];
			GetModuleFileNameA(NULL, buffer, 0x400);
			lua_pushstring(ls, "process");
			lua_pushstring(ls,buffer);
			lua_settable(ls, -3);
		}

		lua_pushstring(ls, "wnd_class");
		lua_pushcfunction(ls, lfunc_wnd_class);
		lua_settable(ls, -3);

		lua_pushstring(ls, "wnd_title");
		lua_pushcfunction(ls, lfunc_wnd_title);
		lua_settable(ls, -3);

		lua_pushstring(ls, "wnd_proc");
		lua_pushcfunction(ls, lfunc_wnd_proc);
		lua_settable(ls, -3);

		lua_pushstring(ls, "wnd_rect");
		lua_pushcfunction(ls, lfunc_wnd_rect);
		lua_settable(ls, -3);

		lua_setglobal(ls, "NBvision");
	}

public:
	lua_vm(void) : pid(0), ls(nullptr) {}

	~lua_vm(void) {
		close();
	}

	bool open(void) {
		close();

		heap = HeapCreate(0, 0, 0);
		if (heap == NULL)
			return false;
		pid = GetCurrentProcessId();
		ls = lua_newstate(allocator, heap);
		if (ls) {
			lua_atpanic(ls, panic);
			luaL_openlibs(ls);
			open_lib_NBvision();
		}
		report("open");
		return ls ? true : false;
	}

	void close(void) {
		report("close");
		if (ls) {
			lua_close(ls);
			ls = nullptr;
		}
		if (heap) {
			HeapDestroy(heap);
			heap = NULL;
		}
		pid = 0;
	}

	int load(const void* buf, size_t len) {
		if (!ls)
			return LUA_ERRMEM;
		reader_info block = { (const char*)buf,len };
		int res = lua_load(ls, reader, &block, "main", nullptr);

		if (res != LUA_OK) {
			report(lua_tostring(ls, -1));
			return res;
		}
		if (!lua_isfunction(ls, -1)) {
			report("not lua function");
			return -1;
		}

		res = lua_pcall(ls, 0, 1, 0);
		if (res != LUA_OK) {
			report(lua_tostring(ls, -1));
			return res;
		}
		if (!lua_isfunction(ls, -1)) {
			report("not lua function");
			return -1;
		}

		return LUA_OK;
	}

	bool call(HWND hwnd, int op,void* lparam) {
		if (!ls) {
			report("corrupted lvm");
			return true;
		}
		lua_pushvalue(ls, -1);
		lua_pushlightuserdata(ls, hwnd);
		lua_pushinteger(ls, op);

		switch (op) {
		case HCBT_CREATEWND:
		{
			auto info = (CBT_CREATEWND*)lparam;
			lua_createtable(ls, 0, 6);

			lua_pushstring(ls, "insert_after");
			lua_pushlightuserdata(ls, info->hwndInsertAfter);
			lua_settable(ls, -3);

			RECT rect = { info->lpcs->x,info->lpcs->y,info->lpcs->x + info->lpcs->cx ,info->lpcs->y + info->lpcs->cy };
			lua_pushstring(ls, "rect");
			ltab_make_rect(ls, &rect);
			lua_settable(ls, -3);

			lua_pushstring(ls, "class");
			if (info->lpcs->lpszClass) {
				if (info->lpcs->lpszClass > (void*)0xFFFF)
					lua_pushstring(ls, info->lpcs->lpszClass);
				else
					lua_pushinteger(ls, (ATOM)info->lpcs->lpszClass);
			}
			else
				lua_pushnil(ls);
			lua_settable(ls, -3);

			lua_pushstring(ls, "title");
			if (info->lpcs->lpszName)
				lua_pushstring(ls, info->lpcs->lpszName);
			else
				lua_pushnil(ls);
			lua_settable(ls, -3);

			{
				lua_pushstring(ls, "style");
				lua_createtable(ls, 2, 0);

				lua_pushinteger(ls, 1);
				lua_pushinteger(ls, info->lpcs->style);
				lua_settable(ls, -3);

				lua_pushinteger(ls, 2);
				lua_pushinteger(ls, info->lpcs->dwExStyle);
				lua_settable(ls, -3);

				lua_settable(ls, -3);
			}

			lua_pushstring(ls, "parent");
			lua_pushlightuserdata(ls, info->lpcs->hwndParent);
			lua_settable(ls, -3);

			break;
		}
		case HCBT_ACTIVATE:
		{
			auto info = (CBTACTIVATESTRUCT*)lparam;

			lua_createtable(ls, 0, 2);
			lua_pushstring(ls, "current_active");
			lua_pushlightuserdata(ls, info->hWndActive);
			lua_settable(ls, -3);
			
			lua_pushstring(ls, "active_by_mouse");
			lua_pushboolean(ls, info->fMouse);
			lua_settable(ls, -3);

			break;
		}
		case HCBT_MINMAX:
		{
			lua_pushinteger(ls, (WORD)lparam);
			break;
		}
		case HCBT_MOVESIZE:
		{
			ltab_make_rect(ls,(RECT*)lparam);
			break;
		}
		case HCBT_SETFOCUS:
		{
			lua_pushlightuserdata(ls,(HWND)lparam);
			break;
		}
		default:
			lua_pushnil(ls);
		}

		int res = lua_pcall(ls, 3, 1, 0);
		if (res != LUA_OK) {
			report(lua_tostring(ls, -1));
			return true;
		}
		if (!lua_isboolean(ls, -1)) {
			report("not boolean value");
			return true;
		}
		bool ret = lua_toboolean(ls, -1) ? true : false;
		lua_pop(ls, 1);
		return ret;
	}
}lvm;







extern "C" __declspec(dllexport)
USNLIB::shared_stream_base* setup(const void* str,size_t len) {
	
	script_length = min(len, sizeof(script));
	memcpy(script, str, script_length);

	return &stream;
}






extern "C" __declspec(dllexport)
LRESULT CALLBACK cbt_hook_proc(int nCode, WPARAM wparam, LPARAM lparam) {
	static std::mutex sync;

	std::lock_guard<std::mutex> lock(sync);

	static volatile bool status = (lvm.open() && (0 == lvm.load(script, script_length)));
	//char buffer[0x400];
	//GetModuleFileName(NULL, buffer, 0x400);
	//stream_write(buffer);
	//snprintf(buffer, 0x400, "\t%p %d %s\n", (HWND)wparam, nCode,status ? "true" : "false");
	//stream_write(buffer);
	//stream_flush();


	try {
		if (status && false == lvm.call((HWND)wparam, nCode, (void*)lparam))
			return -1;
	}
	catch (std::exception& e) {
		stream_write(e.what());
		stream_write("\tshutting down\n");
		stream_flush();
		status = false;
	}

	//RECT* rect = nullptr;
	//switch (nCode) {
	//case HCBT_MOVESIZE:
	//	rect = (RECT*)lparam;
	//case HCBT_CREATEWND:
	//case HCBT_ACTIVATE:
	//	if (judge_popup((HWND)wparam, rect))
	//		return -1;
	//}
	return CallNextHookEx(NULL, nCode, wparam, lparam);
}



BOOL WINAPI DllMain(
	_In_ HINSTANCE hinstDLL,
	_In_ DWORD     fdwReason,
	_In_ LPVOID    lpvReserved
) {
	if (fdwReason == DLL_PROCESS_DETACH && !lpvReserved) {
		lvm.close();
	}
	return TRUE;
}

/*

bool judge_popup(HWND hwnd, RECT* rect) {
	stringstream ss;
	bool result = false;
	do {
		ss << hex << hwnd << '\t';
		char name[0x400];
		if (0 == GetClassNameA(hwnd, name, 0x400)) {
			break;
		}
		ss << name << '\t';

		static const unordered_set<string> blacklist = { "SDL_app" ,"vguiPopupWindow" };

		for (auto it = blacklist.cbegin(); it != blacklist.cend(); ++it) {
			if (*it == name) {
				ss << "class";
				result = true;
				break;
			}
		}

		if (!rect)
			break;

		static POINT screen = { GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN) };

		if (rect->right > 0.9 * screen.x && rect->bottom > 0.8 * screen.y)
			;
		else
			break;

		if (rect->right - rect->left < 0.2 * screen.x && rect->bottom - rect->top < 0.2 * screen.y)
			;
		else
			break;

		PostMessage(hwnd, WM_CLOSE, 0, 0);
		ss << "rect";
		result = true;

	} while (false);

	ss << endl;
	string str = ss.str();
	stream.write(str.c_str(), str.size());

	return result;
}

*/