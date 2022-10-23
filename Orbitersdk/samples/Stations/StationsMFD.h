#ifndef _H_SIMPLE_MFD_CONTROL_COMPUTEREX_H_
#define _H_SIMPLE_MFD_CONTROL_COMPUTEREX_H_
#pragma once

#include <orbitersdk.h>

class SimpleMFD: public MFD {
public:
	SimpleMFD(DWORD w, DWORD h, VESSEL *vessel);
	~SimpleMFD();
	void Update (HDC hDC);
	bool ConsumeKeyBuffered(DWORD key);

	std::string base_name;
	std::string base_desc;
	bool monitor_flag;
	double simt;
	
	static int MsgProc (UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam);
	DWORD width, height;
};
#endif