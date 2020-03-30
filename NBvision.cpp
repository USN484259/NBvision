#include "ui.hpp"
#include <Windows.h>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <cassert>
#include "shared_stream.hpp"
using namespace std;

#define PAGE_SIZE 0x1000
LPCTSTR mutex_name = TEXT("io.github.USN484259@NBvision");
#ifdef WIN64
LPCTSTR dll_name = TEXT("NBvision_hook_64.dll");
#else
LPCTSTR dll_name = TEXT("NBvision_hook_32.dll");
#endif

typedef LRESULT(CALLBACK *dll_cbt_proc)(int, WPARAM, LPARAM);
typedef USNLIB::shared_stream_base* (*dll_setup)(const char*,size_t);



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

int WINAPI WinMain(
	HINSTANCE,
	HINSTANCE,
	LPSTR     lpCmdLine,
	int
) {
	HANDLE hMutex = NULL;
	HANDLE file_mapping = NULL;
	PVOID map_view = nullptr;
	HANDLE pear_proc = NULL;
	USNLIB::shared_stream_base* pear_stream = nullptr;
	HMODULE dll = NULL;
	HHOOK hook = NULL;
#ifdef WIN64
	HANDLE thread_handle = NULL;
	ofstream log_file;
#endif
	do {
#ifdef WIN64
		hMutex = CreateMutex(NULL, TRUE, mutex_name);
		if (!hMutex)
			break;
		if (ERROR_ALREADY_EXISTS == GetLastError())
			break;




		log_file.open("NBvision.log",ios::app);
		if (!log_file.is_open())
			break;

		log_file << "---------NBvision-----------" << endl;

		{
			SECURITY_ATTRIBUTES attrib = { sizeof(SECURITY_ATTRIBUTES),NULL,TRUE };
			file_mapping = CreateFileMapping(INVALID_HANDLE_VALUE, &attrib, PAGE_READWRITE | SEC_COMMIT, 0, PAGE_SIZE, NULL);
			if (!file_mapping)
				break;


		}

#else

		hMutex = OpenMutex(SYNCHRONIZE, FALSE, mutex_name);
		if (!hMutex)
			break;

#ifdef _DEBUG
		WaitForSingleObject(hMutex, INFINITE);
#endif

		{	//get HANDLEs from cmdline
			DWORD tmp;
			file_mapping = (HANDLE)strtoull(lpCmdLine, &lpCmdLine, 16);
			if (!GetHandleInformation(file_mapping, &tmp)) {
				file_mapping = NULL;
				break;
			}
			pear_proc = (HANDLE)strtoull(lpCmdLine, &lpCmdLine, 16);
			if (!GetHandleInformation(pear_proc, &tmp)) {
				pear_proc = NULL;
				break;
			}

			while (*lpCmdLine && isspace(*lpCmdLine))
				++lpCmdLine;
		}


#endif

		map_view = MapViewOfFile(file_mapping, FILE_MAP_WRITE, 0, 0, PAGE_SIZE);
		if (!map_view)
			break;

		pear_stream = new(map_view) USNLIB::shared_stream<4000>();


		dll = LoadLibraryEx(dll_name, NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
		if (!dll)
			break;

		auto setup = (dll_setup)GetProcAddress(dll, "setup");
		auto cbt_proc = (dll_cbt_proc)GetProcAddress(dll, "cbt_hook_proc");

		if (setup && cbt_proc)
			;
		else
			break;

		USNLIB::shared_stream_base* stream = nullptr;
		{
			ifstream script_file(lpCmdLine);
			stringstream script_stream;
			if (script_file.is_open())
				script_stream << script_file.rdbuf();
			else
				script_stream << "return function() return true end\n";
			string script = script_stream.str();
			stream = setup(script.c_str(), script.size());
			if (!stream)
				break;
		}

		hook = SetWindowsHookExA(WH_CBT, cbt_proc, dll, 0);
		if (!hook)
			break;
#ifdef WIN64

		{
			HANDLE cur_handle = GetCurrentProcess();
			HANDLE dup_handle;
			if (!DuplicateHandle(cur_handle, cur_handle, cur_handle, &dup_handle, SYNCHRONIZE, TRUE, 0))
				break;

			stringstream ss;
			ss << "NBvision_32.exe " << hex << (size_t)file_mapping << ' ' << (size_t)dup_handle << ' ' << lpCmdLine;
			string str = ss.str();
			std::vector<char> cmdline(str.cbegin(),str.cend());
			cmdline.push_back(0);

			STARTUPINFOA info = { sizeof(STARTUPINFOA),0 };
			PROCESS_INFORMATION ps = { 0 };

			BOOL res = CreateProcessA(NULL, cmdline.data(), NULL, NULL, TRUE, 0, NULL, NULL, &info, &ps);
			CloseHandle(dup_handle);
			if (!res)
				break;

			CloseHandle(ps.hThread);
			pear_proc = ps.hProcess;
		}

#ifdef _DEBUG
		MessageBox(NULL, NULL, TEXT("NBvision"), MB_OK);
		ReleaseMutex(hMutex);
#endif

		thread_handle = CreateThread(NULL, 0, thread_ui, NULL, 0, NULL);
		if (thread_handle == NULL)
			break;

		HANDLE handles[] = { pear_proc,thread_handle };
#endif
		DWORD timeout = 0;
		do {
#ifdef WIN64
			DWORD res = WaitForMultipleObjects(2, handles, FALSE, timeout);
#else
			DWORD res = WaitForSingleObject(pear_proc, timeout);
#endif
			char buffer[0x400];
			timeout = min(2 * max(timeout, 1),1000);

#ifdef WIN64
			do {
				size_t len = pear_stream->read(buffer, 0x400);
				if (!len)
					break;
				log_file.write(buffer, len);
				log_file.flush();
				timeout = 0;
			} while (true);
#endif
			do {
				size_t len = stream->read(buffer,0x400);
				if (!len)
					break;
#ifdef WIN64
				log_file.write(buffer, len);
				log_file.flush();
#else
				pear_stream->write(buffer, len);
#endif
				timeout = 0;
			} while (true);



			if (res != WAIT_TIMEOUT)
				break;

		} while (true);


	} while (false);
	

	if (hook)
		UnhookWindowsHookEx(hook);
	if (dll)
		FreeLibrary(dll);
	if (pear_stream)
		pear_stream->~shared_stream_base();
	if (pear_proc)
		CloseHandle(pear_proc);
	if (map_view)
		UnmapViewOfFile(map_view);
	if (file_mapping)
		CloseHandle(file_mapping);
	if (hMutex)
		CloseHandle(hMutex);
#ifdef WIN64
	if (thread_handle)
		CloseHandle(thread_handle);
	log_file.close();
#endif
	return 0;
}