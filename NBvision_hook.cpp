#include <Windows.h>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <unordered_set>
#include "shared_stream.hpp"
using namespace std;

#pragma section(".shared",read,write,shared)

__declspec(allocate(".shared"))
USNLIB::shared_stream<4000> stream;

__declspec(allocate(".shared"))
alignas(0x1000)
char config[0x1000];



extern "C" __declspec(dllexport)
USNLIB::shared_stream_base* setup(const char* cfg) {
	strcpy_s(config, cfg);
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