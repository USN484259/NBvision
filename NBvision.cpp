#include "ui.hpp"
#include <Windows.h>
#include <stdexcept>
#include <fstream>
#include <cassert>
#include "shared_stream.hpp"
using namespace std;

LPCTSTR mutex_name = TEXT("io.github.USN484259@NBvision");


typedef LRESULT(CALLBACK *dll_cbt_proc)(int, WPARAM, LPARAM);
typedef USNLIB::shared_stream_base* (*dll_setup)(const char*);



DWORD CALLBACK thread_ui(PVOID) {
	DWORD res = 0;
	try {
		ui taskbar_icon;
		taskbar_icon.show();
	}
	catch (exception&) {
		res = (DWORD)-1;
	}

	return res;
}

int WinMain(
	HINSTANCE,
	HINSTANCE,
	LPSTR     lpCmdLine,
	int
) {
	HANDLE hMutex = NULL;
	HANDLE thread_handle = NULL;
	HMODULE dll = NULL;
	HHOOK hook = NULL;
	ofstream log_file;

	do {
		hMutex = CreateMutex(NULL, TRUE, mutex_name);
		if (!hMutex)
			break;
		if (ERROR_ALREADY_EXISTS == GetLastError())
			break;

		log_file.open("NBvision.log",ios::app);
		if (!log_file.is_open())
			break;

		log_file << "---------NBvision-----------" << endl;

		dll = LoadLibraryEx(TEXT("NBvision_hook.dll"), NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
		if (!dll)
			break;

		auto setup = (dll_setup)GetProcAddress(dll, "setup");
		auto cbt_proc = (dll_cbt_proc)GetProcAddress(dll, "cbt_hook_proc");

		if (setup && cbt_proc)
			;
		else
			break;

		auto stream = setup("");


		hook = SetWindowsHookExA(WH_CBT, cbt_proc, dll, 0);
		if (!hook)
			break;

		thread_handle = CreateThread(NULL, 0, thread_ui, NULL, 0, NULL);
		if (thread_handle == NULL)
			break;

		do {
			DWORD res = WaitForSingleObject(thread_handle, 1000);

			char buffer[0x400];
			do {
				size_t len = stream->read(buffer,0x400);
				if (!len)
					break;
				assert(log_file.good());
				log_file.write(buffer, len);

			} while (true);

			log_file.flush();

			if (res != WAIT_TIMEOUT)
				break;

		} while (true);



	} while (false);
	

	if (hook)
		UnhookWindowsHookEx(hook);
	if (dll)
		FreeLibrary(dll);

	CloseHandle(thread_handle);
	CloseHandle(hMutex);
	log_file.close();
	return 0;
}