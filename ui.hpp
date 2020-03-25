#pragma once
#include "gui.hpp"


class ui : public USNLIB::gui {
	HMENU menu;
	const UINT TaskbarCreated;

	static const GUID guid;

	bool place_icon(bool);

protected:
	
	LRESULT msg_proc(UINT, WPARAM, LPARAM) override;

public:
	ui(void);
	~ui(void);



};