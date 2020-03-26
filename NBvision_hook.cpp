#include <Windows.h>
#include <cstdio>
#include "shared_stream.hpp"
#include "lua.h"

#pragma comment(lib,"lua_vm.lib")




#pragma section(".shared",read,write,shared)

__declspec(allocate(".shared"))
USNLIB::shared_stream<0x0800> stream;

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

void stream_write(const char* str) {
	lua_stream.append(str);
}

void stream_flush(void) {
	lua_stream.append("\n");
	stream.write(lua_stream.buffer, lua_stream.count);
	lua_stream.count = 0;
}


class lua_vm {
	lua_State* ls;
	DWORD pid;

	void report(const char* msg) {
		char str[0x20];
		snprintf(str,0x20, "%X\t", pid);
		stream_write(str);
		stream_write(msg);
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


	struct reader_info {
		const char* buf;
		size_t len;
	};

	static const char* reader(lua_State* ls, void* ud, size_t* size) {
		reader_info* block = (reader_info*)ud;
		const char* res = block->buf;
		*size = block->len;

		block->buf = nullptr;
		block->len = 0;

		return res;
	}


public:
	lua_vm(void) : pid(0), ls(nullptr) {}

	~lua_vm(void) {
		close();
	}

	bool open(HANDLE heap) {
		close();
		pid = GetCurrentProcessId();
		ls = lua_newstate(allocator, heap);
		return ls && pid;
	}

	void close(void) {
		if (ls) {
			lua_close(ls);
			ls = nullptr;
		}
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

	bool call(HWND hwnd, int op) {
		if (!ls) {
			report("corrupted lvm");
			return true;
		}
		lua_pushvalue(ls, -1);
		lua_pushlightuserdata(ls, hwnd);
		lua_pushinteger(ls, op);
		int res = lua_pcall(ls, 2, 1, 0);
		if (res != LUA_OK) {
			report(lua_tostring(ls, -1));
			return true;
		}
		if (!lua_isboolean(ls, -1)) {
			report("not boolean value");
			return true;
		}
		bool res = lua_toboolean(ls, -1) ? true : false;
		lua_pop(ls, 1);
		return res;
	}
}lvm;







extern "C" __declspec(dllexport)
USNLIB::shared_stream_base* setup(const void* str,size_t len) {
#error TODO
	lvm.open(heap);
	memcpy(script, str, min(len,sizeof(script)));
	return &stream;
}





bool judge_popup(HWND, RECT*);

extern "C" __declspec(dllexport)
LRESULT CALLBACK cbt_hook_proc(int nCode, WPARAM wparam, LPARAM lparam) {
	stringstream ss;
	char buffer[0x400];
	GetModuleFileName(NULL, buffer, 0x400);

	time_t t = time(nullptr);
	tm time_structure;
	localtime_s(&time_structure, &t);
	ss << put_time(&time_structure, "%F %T%t\t") << buffer << '\t' << hex << (HWND)wparam << '\t' << nCode << endl;

	string str = ss.str();

	stream.write(str.c_str(), str.size());


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