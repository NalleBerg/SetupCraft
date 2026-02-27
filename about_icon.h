#pragma once
#include <windows.h>
#include <string>
#include <map>

// Create the About icon control. Returns the HWND of created control.
HWND CreateAboutIconControl(HWND parent, HINSTANCE hInst, int x, int y, int size, int id,
	const std::map<std::wstring, std::wstring>& locale);

// Handle mouse move events (call from parent WM_MOUSEMOVE)
void AboutIcon_OnMouseMove(HWND parent, WPARAM wParam, LPARAM lParam, const std::map<std::wstring, std::wstring>& locale);

// Handle left-button clicks (call from parent WM_LBUTTONDOWN)
void AboutIcon_OnLButtonDown(HWND parent, WPARAM wParam, LPARAM lParam);

// Cleanup any internal state (call on WM_DESTROY or shutdown)
void AboutIcon_Cleanup();
