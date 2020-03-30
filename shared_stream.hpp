#pragma once
#include <windows.h>

#define PAGE_SIZE 0x1000



class shared_stream {

	enum : size_t { SIZE = PAGE_SIZE - 2 * sizeof(DWORD) };

	volatile DWORD sync;
	WORD r, w;
	BYTE buffer[SIZE];


	inline void lock(void) {
		while (true) {
			long old = InterlockedCompareExchange(&sync, 1, 0);
			if (old == 0)
				break;
			_mm_pause();
		}
	}

	inline void unlock(void) {
		InterlockedExchange(&sync, 0);
	}

public:

	shared_stream(void) : sync(0), r(0), w(0) {}

	size_t read(void* dst, size_t lim) {
		lock();

		size_t index = 0;

		while (r != w && index < lim) {
			((BYTE*)dst)[index++] = buffer[r++];
			r %= SIZE;
		}
		unlock();
		return index;
	}

	size_t write(const void* sor, size_t cnt) {
		lock();

		size_t index = 0;
		while (index < cnt) {
			if (r == (w + 1) % SIZE)
				break;
			buffer[w++] = ((const BYTE*)sor)[index++];
			w %= SIZE;
		}
		unlock();
		return index;
	}


};

static_assert(sizeof(shared_stream) == PAGE_SIZE,"shared_stream size mismatch");