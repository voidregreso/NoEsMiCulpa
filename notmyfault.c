#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "resource.h"
#include "ioctlcmd.h"
#include "driver.h"

COLORREF BsodFg = RGB(0xFF, 0xFF, 0xFF);
COLORREF BsodBg = RGB(0xFF, 0, 0);
#define MAX_ERR_MSG_SIZE 512

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

//----------------------------------------------------------------------
//
// Abort
//
// Exit with a fatal error.
//
//----------------------------------------------------------------------
LONG Abort(HWND hWnd, const char *Msg, DWORD Error)
{
	LPVOID lpMsgBuf;
	char errmsg[MAX_ERR_MSG_SIZE];

	// Format the error message
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
					  NULL, Error,
					  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					  (LPSTR)&lpMsgBuf, 0, NULL) == 0)
	{
		// Handle FormatMessage failure
		strncpy(errmsg, "Error but no messages", MAX_ERR_MSG_SIZE);
		errmsg[MAX_ERR_MSG_SIZE - 1] = '\0'; // Ensure null-termination
	}
	else
	{
		// Construct the error message
		snprintf(errmsg, MAX_ERR_MSG_SIZE, "%s: %s", Msg, (char *)lpMsgBuf);
		errmsg[MAX_ERR_MSG_SIZE - 1] = '\0'; // Ensure null-termination

		// Specific error handling
		if (Error == ERROR_INVALID_HANDLE || Error == ERROR_ACCESS_DENIED ||
			Error == ERROR_FILE_NOT_FOUND)
		{
			snprintf(errmsg, MAX_ERR_MSG_SIZE,
					 "%s\nMake sure that you are an administrator and that NotMyFault is not already running.",
					 errmsg);
			errmsg[MAX_ERR_MSG_SIZE - 1] = '\0'; // Ensure null-termination
		}

		LocalFree(lpMsgBuf);
	}

	// Display the message box
	MessageBox(hWnd, errmsg, "NotMyFault", MB_OK | MB_ICONERROR);

	// Exit the application
	PostQuitMessage(1);

	return (DWORD)-1;
}

VOID CenterWindow(HWND hDlg)
{
	RECT rect;

	// Get the window rectangle
	GetWindowRect(hDlg, &rect);

	// Calculate the center position
	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);
	int xPos = (screenWidth - rect.right) / 2;
	int yPos = (screenHeight - rect.bottom) / 2;

	// Adjust the position to align to a multiple of 8
	xPos = (xPos + 4) & ~7;

	// Center the window
	MoveWindow(hDlg, xPos, yPos, rect.right, rect.bottom, 0);
}

UINT_PTR CALLBACK BsodColorsCallback(HWND hDlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
	static COLORREF newFg, newBg;
	static UINT wm_colorOkString, wm_setRgbString;
	HBRUSH hBack;

	switch (uiMsg)
	{
	case WM_INITDIALOG:
		newFg = BsodFg;
		newBg = BsodBg;
		wm_colorOkString = RegisterWindowMessage(COLOROKSTRING);
		wm_setRgbString = RegisterWindowMessage(SETRGBSTRING);
		CheckRadioButton(hDlg, IDC_RADIOFG, IDC_RADIOBG, IDC_RADIOFG);
		SendMessage(hDlg, wm_setRgbString, 0, newFg);
		SetFocus(GetDlgItem(hDlg, IDC_DONE));
		break;

	case WM_CTLCOLORSTATIC:
		if ((HWND)lParam == GetDlgItem(hDlg, IDC_PREVIEW))
		{
			SetBkColor((HDC)wParam, newBg);
			SetTextColor((HDC)wParam, newFg);
			hBack = CreateSolidBrush(newBg);
			return hBack != NULL;
		}
		break;

	case WM_COMMAND:
		if (wParam == IDC_DONE)
		{
			BsodFg = newFg;
			BsodBg = newBg;
			PostMessage(hDlg, WM_COMMAND, IDABORT, 1);
			return FALSE;
		}
		break;

	default:
		if (uiMsg == wm_colorOkString)
		{
			CHOOSECOLOR *choose = (CHOOSECOLOR *)lParam;
			if (IsDlgButtonChecked(hDlg, IDC_RADIOBG))
			{
				newBg = choose->rgbResult;
				InvalidateRect(GetDlgItem(hDlg, IDC_PREVIEW), NULL, TRUE);
				return TRUE;
			}
			else
			{
				newFg = choose->rgbResult;
				InvalidateRect(GetDlgItem(hDlg, IDC_PREVIEW), NULL, TRUE);
				return TRUE;
			}
		}
		break;
	}
	return 0;
}

//----------------------------------------------------------------------
//
//  StartMyFaultDriver
//
// Loads and starts the driver.
//
//----------------------------------------------------------------------
LONG StartMyFaultDriver(HWND hDlg)
{
	char driverPath[MAX_PATH];
	char systemRoot[MAX_PATH];
	char path[MAX_PATH];
	WIN32_FIND_DATA findData;
	HANDLE findHandle;
	char *file;
	DWORD error;
	char msgbuf[MAX_PATH * 2];

	//
	// Load the myfault driver
	//
	GetCurrentDirectory(sizeof path, path);
	sprintf(path + lstrlen(path), "\\%s", SYS_FILE);

	findHandle = FindFirstFile(path, &findData);
	if (findHandle == INVALID_HANDLE_VALUE)
	{
		if (!SearchPath(NULL, SYS_FILE, NULL, sizeof(path), path, &file))
		{
			sprintf(msgbuf, "%s was not found.", SYS_FILE);
			return Abort(hDlg, msgbuf, GetLastError());
		}
	}
	else
		FindClose(findHandle);

	if (!GetEnvironmentVariable("SYSTEMROOT", systemRoot, sizeof(systemRoot)))
	{
		strcpy(msgbuf, "Could not resolve SYSTEMROOT environment variable");
		return Abort(hDlg, msgbuf, GetLastError());
	}
	sprintf(driverPath, "%s\\system32\\drivers\\myfault.sys", systemRoot);
	SetFileAttributes(driverPath, FILE_ATTRIBUTE_NORMAL);
	CopyFile(path, driverPath, FALSE);
	if (!LoadDeviceDriver(SYS_NAME, driverPath, &SysHandle, &error))
	{

		if (!CopyFile(path, driverPath, FALSE))
		{
			sprintf(msgbuf, "Unable to copy %s to %s\n\n"
							"Make sure that %s is in the current directory.",
					SYS_NAME, driverPath, SYS_FILE);
			return Abort(hDlg, msgbuf, GetLastError());
		}
		SetFileAttributes(driverPath, FILE_ATTRIBUTE_NORMAL);
		if (!LoadDeviceDriver(SYS_NAME, driverPath, &SysHandle, &error))
		{

			UnloadDeviceDriver(SYS_NAME);
			if (!LoadDeviceDriver(SYS_NAME, driverPath, &SysHandle, &error))
			{
				sprintf(msgbuf, "Error loading %s:", path);
				DeleteFile(driverPath);
				return Abort(hDlg, msgbuf, error);
			}
		}
	}
	return TRUE;
}

void IoctlThreadProc(PVOID Context)
{
	DWORD nb;
	DeviceIoControl(SysHandle, (DWORD)Context, NULL, 0, NULL, 0, &nb, NULL);
}

void formatAllocSize(DWORD allocSize, UINT PoolType, CHAR *res)
{
	double size = (double)allocSize;
	const char *units[] = {"B", "KB", "MB", "GB", "TB"};
	int unitIndex = 0;

	while (size >= 1024 && unitIndex < 4)
	{
		size /= 1024;
		unitIndex++;
	}

	if (unitIndex == 0)
	{
		sprintf(res, "%s: %.0lf%s", (PoolType ? "NP" : "P"), size, units[unitIndex]);
	}
	else
	{
		sprintf(res, "%s: %.2lf%s", (PoolType ? "NP" : "P"), size, units[unitIndex]);
	}
}

void LeakPool(HWND hDlg, UINT PoolType, DWORD allocSize)
{
	DWORD maxAlloc = allocSize;
	DWORD bytesAllocated = 0;
	DWORD tickCount = GetTickCount();
	BOOL shouldContinue = TRUE;
	CHAR res[1024];
	static DWORD npLeakSize = 0, pLeakSize = 0;

	while (shouldContinue && bytesAllocated < maxAlloc && GetTickCount() - tickCount < 1000)
	{
		if (!DeviceIoControl(SysHandle,
							 PoolType ? IOCTL_LEAK_NONPAGED : IOCTL_LEAK_PAGED,
							 &allocSize, sizeof(allocSize),
							 NULL, 0, NULL, NULL))
		{
			// Reduce the allocation size until it is 1
			allocSize = (allocSize > 1) ? allocSize / 2 : 1;
		}
		else
		{
			bytesAllocated += allocSize;
			if (bytesAllocated < maxAlloc)
			{
				// If there is room, try a larger allocation
				allocSize = (allocSize < 8192) ? allocSize * 2 : 8192;
			}
			else
			{
				// Maximum allocation reached, end cycle
				shouldContinue = FALSE;
			}
		}
		if (PoolType)
		{
			npLeakSize += bytesAllocated;
			formatAllocSize(npLeakSize, PoolType, res);
			SetDlgItemText(hDlg, IDC_STANPAGE, res);
		}
		else
		{
			pLeakSize += bytesAllocated;
			formatAllocSize(pLeakSize, PoolType, res);
			SetDlgItemText(hDlg, IDC_STAPAGE, res);
		}
	}
}

DWORD hexStringToDWORD(const char *str)
{
	DWORD def = 0xdeaddead;
	if (str == NULL || *str == '\0')
	{
		return def; // empty string
	}

	char *endPtr;
	errno = 0;
	unsigned long result = strtoul(str, &endPtr, 16);

	if ((errno == ERANGE && (result == ULONG_MAX || result == 0)) || (*endPtr != '\0' && !isspace((unsigned char)*endPtr)))
	{
		return def; // failed because it's not a pure hex string
	}

	return (DWORD)result;
}

//----------------------------------------------------------------------
//
// MainDialog
//
// This is the main window.
//
//----------------------------------------------------------------------
LRESULT APIENTRY MainDialog(HWND hDlg, UINT message, UINT wParam,
							LONG lParam)
{
	char label[MAX_PATH];
	SYSTEM_INFO sysInfo;
	DWORD i, nb, ioctl = 0;
	DWORD allocSize, maxAlloc, errCode;
	static BOOLEAN leakPaged = FALSE;
	static BOOLEAN leakNonpaged = FALSE;
	CHOOSECOLOR colorArgs;
	static DWORD rgbCurrent;
	static COLORREF acrCustClr[16];
	HWND hEdit = GetDlgItem(hDlg, IDC_LEAKMB);

	switch (message)
	{
	case WM_INITDIALOG:

		//
		// Start driver
		//
		if (!StartMyFaultDriver(hDlg))
			return FALSE;

		//
		// We can delete the driver and its Registry key now that its loaded
		//
		CheckDlgButton(hDlg, IDC_IRQL, BST_CHECKED);
		CenterWindow(hDlg);
		SetDlgItemText(hDlg, IDC_LEAKMB, "8192");
		break;

	case WM_TIMER:
		GetDlgItemText(hDlg, IDC_LEAKMB, label, _countof(label));
		allocSize = maxAlloc = (atoi(label) * 1024);
		LeakPool(hDlg, wParam, allocSize);
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK:
			if (IsDlgButtonChecked(hDlg, IDC_IRQL) == BST_CHECKED)
			{
				ioctl = IOCTL_IRQL;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_BUFFEROVERFLOW) == BST_CHECKED)
			{
				ioctl = IOCTL_BUFFER_OVERFLOW;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_WILDPOINTER) == BST_CHECKED)
			{
				ioctl = IOCTL_WILD_POINTER;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_STACKTRASH) == BST_CHECKED)
			{
				ioctl = IOCTL_TRASH_STACK;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_PAGEFAULT) == BST_CHECKED)
			{
				ioctl = IOCTL_PAGE_FAULT;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_STACKOFLOW) == BST_CHECKED)
			{
				ioctl = IOCTL_STACK_OVERFLOW;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_DEBUGBREAK) == BST_CHECKED)
			{
				ioctl = IOCTL_HC_BREAKPOINT;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_DOUBLEFREE) == BST_CHECKED)
			{
				ioctl = IOCTL_DOUBLE_FREE;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_HANG) == BST_CHECKED)
			{
				ioctl = IOCTL_HANG;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_DEADLOCK) == BST_CHECKED)
			{
				ioctl = IOCTL_DEADLOCK;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_HANGIRP) == BST_CHECKED)
			{
				_beginthread(IoctlThreadProc, 0, (PVOID)IOCTL_HANG_IRP);
				break;
			}
			else if (IsDlgButtonChecked(hDlg, IDC_DIYERROR) == BST_CHECKED)
			{
				GetDlgItemText(hDlg, IDC_ERRCODE, label, _countof(label));
				errCode = hexStringToDWORD(label);
				DeviceIoControl(SysHandle, IOCTL_DIY_BSOD, &errCode, sizeof(DWORD), NULL, 0, &nb, NULL);
				break;
			}

			// Execute hang and deadlock on each CPU
			if (ioctl == IOCTL_HANG || ioctl == IOCTL_DEADLOCK)
			{
				GetSystemInfo(&sysInfo);
				for (i = 0; i < sysInfo.dwNumberOfProcessors; i++)
				{
					DeviceIoControl(SysHandle, ioctl, NULL, 0, NULL, 0, &nb, NULL);
				}
			}
			else
			{
				DeviceIoControl(SysHandle, ioctl, NULL, 0, NULL, 0, &nb, NULL);
			}
			break;

		case IDC_LEAK_PAGE:
			if (leakPaged)
			{
				KillTimer(hDlg, 0);
				EnableWindow(hEdit, TRUE);
				SetDlgItemText(hDlg, IDC_LEAK_PAGE, "Leak &Paged");
				SetDlgItemText(hDlg, IDC_STAPAGE, "P: Inactive");
			}
			else
			{
				SetTimer(hDlg, 0, 1000, NULL);
				EnableWindow(hEdit, FALSE);
				SetDlgItemText(hDlg, IDC_LEAK_PAGE, "Stop &Paged");
			}
			leakPaged = !leakPaged;
			break;

		case IDC_LEAK_NONPAGE:
			if (leakNonpaged)
			{
				KillTimer(hDlg, 1);
				EnableWindow(hEdit, TRUE);
				SetDlgItemText(hDlg, IDC_LEAK_NONPAGE, "Leak &Nonpaged");
				SetDlgItemText(hDlg, IDC_STANPAGE, "NP: Inactive");
			}
			else
			{
				SetTimer(hDlg, 1, 1000, NULL);
				EnableWindow(hEdit, FALSE);
				SetDlgItemText(hDlg, IDC_LEAK_NONPAGE, "Stop &Nonpaged");
			}
			leakNonpaged = !leakNonpaged;
			break;

		case IDCOLOR:
		{
			COLORREF CustomColors[16];
			int j;
			for (j = 0; j < 16; j++)
			{
				CustomColors[j] = RGB(255, 255, 255);
			}
			colorArgs.lStructSize = sizeof colorArgs;
			colorArgs.Flags = CC_RGBINIT | CC_ENABLEHOOK | CC_ENABLETEMPLATE | CC_FULLOPEN;
			colorArgs.hwndOwner = hDlg;
			colorArgs.hInstance = (HWND)GetModuleHandle(NULL);
			colorArgs.rgbResult = RGB(0, 0, 0);
			colorArgs.lpCustColors = CustomColors;
			colorArgs.lCustData = 0;
			colorArgs.rgbResult = BsodFg;
			colorArgs.lpTemplateName = "BSODCOLORS";
			colorArgs.lpfnHook = BsodColorsCallback;
			if (ChooseColor(&colorArgs) == TRUE)
			{

				LARGE_INTEGER Color;
				Color.LowPart = RGB(GetRValue(BsodBg) / 4,
									GetGValue(BsodBg) / 4,
									GetBValue(BsodBg) / 4);
				Color.HighPart = RGB(GetRValue(BsodFg) / 4,
									 GetGValue(BsodFg) / 4,
									 GetBValue(BsodFg) / 4);
				DeviceIoControl(SysHandle, IOCTL_BSOD_COLOR, &Color, sizeof(LARGE_INTEGER), NULL, 0, &nb, NULL);
			}
		}
		break;

		case IDCANCEL:
			EndDialog(hDlg, 0);
			PostQuitMessage(0);
			break;
		}
		break;

	case WM_CLOSE:
		EndDialog(hDlg, 0);
		PostQuitMessage(0);
		break;
	}
	return DefWindowProc(hDlg, message, wParam, lParam);
}

//----------------------------------------------------------------------
//
// WinMain
//
// Initialize a dialog window class and pop the autologon dialog.
//
//----------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,
				   HINSTANCE hPrevInstance,
				   LPSTR lpCmdLine,
				   int nCmdShow)
{
	static TCHAR szAppName[] = TEXT("NOTMYFAULT");
	MSG msg;
	HWND hMainDlg;
	WNDCLASSEX wndclass;

	//
	// Create the main window class
	//
	wndclass.cbSize = sizeof(WNDCLASSEX);
	wndclass.style = CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc = (WNDPROC)MainDialog;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = DLGWINDOWEXTRA;
	wndclass.hInstance = hInstance;
	wndclass.hIcon = LoadIcon(hInstance, "APPICON");
	wndclass.hIconSm = LoadIcon(hInstance, "APPICON");
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = szAppName;
	RegisterClassEx(&wndclass);

	//
	// Create the dialog
	//
	hMainDlg = CreateDialog(hInstance, "NOTMYFAULT", NULL, (DLGPROC)MainDialog);
	ShowWindow(hMainDlg, nCmdShow);

	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!IsDialogMessage(hMainDlg, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int)msg.wParam;
}
