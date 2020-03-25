#include "ui.hpp"
#include "resource.h"
#include <stdexcept>
#include <windowsx.h>

using namespace std;
using namespace USNLIB;

const GUID ui::guid = 
#ifdef _DEBUG
{ 0x2b201507, 0x507c, 0x4b0b,{ 0xad, 0x40, 0xcf, 0xf3, 0xcd, 0xd4, 0xdf, 0xd3 } };
#else
{ 0x9e8ef360, 0xd076, 0x4d7e,{ 0xb6, 0xf7, 0x7e, 0xd8, 0x45, 0xec, 0x58, 0x97 } };
#endif



enum : DWORD { MENU_EXIT = 1, UIM_MENU = WM_USER + 259 };

ui::ui(void) : gui(typeid(ui).name()), menu(CreatePopupMenu()), TaskbarCreated(RegisterWindowMessage(TEXT("TaskbarCreated"))) {
	style = 0;
	if (!menu)
		throw runtime_error("ui CreateMenu");

	if (!AppendMenu(menu,0, MENU_EXIT,TEXT("Exit")))
		throw runtime_error("ui AppendMenu");

	tit = TEXT("NBvision");
}

ui::~ui(void) {
	DestroyMenu(menu);
}

bool ui::place_icon(bool place) {
	NOTIFYICONDATA icon = { 0 };

	icon.cbSize = sizeof(NOTIFYICONDATA);
	icon.hWnd = hwnd;

	icon.guidItem = guid;
	if (!place) {
		icon.uFlags = NIF_GUID;
		return Shell_NotifyIcon(NIM_DELETE, &icon) ? true : false;

	}
	icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID | NIF_SHOWTIP;
	icon.uCallbackMessage = UIM_MENU;
	icon.hIcon = LoadIcon(instance, MAKEINTRESOURCE(MAIN_ICON));
#ifdef UNICODE
	wcscpy_s(icon.szTip, tit.c_str());
#else
	strcpy_s(icon.szTip, tit.c_str());
#endif
	icon.guidItem = guid;


	Shell_NotifyIcon(NIM_DELETE, &icon);
	if (!Shell_NotifyIcon(NIM_ADD, &icon)) {
		GetLastError();
		return false;
	}

	icon.cbSize = sizeof(NOTIFYICONDATA);
	icon.uVersion = NOTIFYICON_VERSION_4;

	if (!Shell_NotifyIcon(NIM_SETVERSION, &icon)) {
		return false;
	}


	return true;
}



LRESULT ui::msg_proc(UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == TaskbarCreated) {
		return place_icon(true) ? 0 : -1;
	}
	switch (msg) {
	case WM_CREATE:
		return place_icon(true) ? 0 : -1;
	case WM_CLOSE:
		place_icon(false);
		break;

	case UIM_MENU:
		switch (LOWORD(lParam)) {
		case WM_LBUTTONUP:
		case WM_CONTEXTMENU:

			SetForegroundWindow(hwnd);


			int selection = TrackPopupMenu(menu, TPM_NONOTIFY | TPM_RETURNCMD | TPM_NOANIMATION, GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam), 0, hwnd, NULL);

			switch (selection) {
			case MENU_EXIT:
				PostMessage(hwnd, WM_CLOSE, 0, 0);
			case 0:
				break;
			}
			return 0;
		}


	}

	return gui::msg_proc(msg, wParam, lParam);
}