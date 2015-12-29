// [InputHooks.cpp 2014-03-12 abright]
// new home for input hooking code

#include "stdafx.h"

#include "TaksiInput.h"

#include "InputHooks.h"
#include "KeyboardInput.h"

#ifdef USE_DIRECTI
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

enum DIDEVICE8_FUNC_TYPE {
	DI8_DEVICE_QueryInterface = 0,
	DI8_DEVICE_AddRef = 1,
	DI8_DEVICE_Release = 2,

	DI8_DEVICE_Acquire = 7,
	DI8_DEVICE_Unacquire = 8,
	DI8_DEVICE_GetDeviceState = 9,
	DI8_DEVICE_GetDeviceData = 10
};

static UINT_PTR s_nDI8_GetDeviceState = 0;
static UINT_PTR s_nDI8_GetDeviceData = 0;

typedef HRESULT (WINAPI *DIRECTINPUT8CREATE)(HINSTANCE, DWORD, REFIID, LPVOID, IDirectInput8**);
static DIRECTINPUT8CREATE s_DirectInput8Create = NULL;

typedef HRESULT (WINAPI *GETDEVICESTATE)(IDirectInputDevice8 *, DWORD, LPVOID);
static GETDEVICESTATE s_DI8_GetDeviceState = NULL;

typedef HRESULT (WINAPI *GETDEVICEDATA)(IDirectInputDevice8 *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD , DWORD);
static GETDEVICEDATA s_DI8_GetDeviceData = NULL;

static CHookJump s_HookDeviceState;
static CHookJump s_HookDeviceData;

#endif

typedef BOOL (WINAPI *GETKEYBOARDSTATE)(PBYTE);
static GETKEYBOARDSTATE s_GetKeyboardState = NULL;

typedef SHORT (WINAPI *GETASYNCKEYSTATE)(int);
static GETASYNCKEYSTATE s_GetAsyncKeyState = NULL;

typedef BOOL (WINAPI *GETCURSORPOS)(LPPOINT);
static GETCURSORPOS s_GetCursorPos = NULL;

typedef BOOL (WINAPI *SETCURSORPOS)(INT, INT);
static SETCURSORPOS s_SetCursorPos = NULL;

typedef UINT (WINAPI *GETRAWINPUTDATA)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
static GETRAWINPUTDATA s_GetRawInputData = NULL;

typedef UINT (WINAPI *GETRAWINPUTBUFFER)(PRAWINPUT, PUINT, UINT);
static GETRAWINPUTBUFFER s_GetRawInputBuffer = NULL;

static CHookJump s_HookGetKeyboardState;
static CHookJump s_HookGetAsyncKeyState;
static CHookJump s_HookGetCursorPos;
static CHookJump s_HookSetCursorPos;
static CHookJump s_HookGetRawInputData;
static CHookJump s_HookGetRawInputBuffer;

#ifdef USE_DIRECTI

bool GetDIHookOffsets( HINSTANCE hInst )
{
	if ( s_nDI8_GetDeviceState || s_nDI8_GetDeviceData )
		return true;

	CDllFile dll;
	HRESULT hRes = dll.FindDll( L"dinput8.dll" );
	if (IS_ERROR(hRes))
	{
		LOG_MSG( "GetDIHookOffsets: Failed to load dinput8.dll (0x%08x)" LOG_CR, hRes );
		return false;
	}

	s_DirectInput8Create = (DIRECTINPUT8CREATE)dll.GetProcAddress( "DirectInput8Create" );
	if (!s_DirectInput8Create) 
	{
		HRESULT hRes = HRes_GetLastErrorDef( HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED) );
		LOG_MSG( "GetDIHookOffsets: lookup for DirectInput8Create failed. (0x%08x)" LOG_CR, hRes );
		return false;
	}

	IRefPtr<IDirectInput8> pDI;
	IRefPtr<IDirectInputDevice8> pDevice;

	hRes = s_DirectInput8Create( hInst, DIRECTINPUT_VERSION, IID_IDirectInput8, IREF_GETPPTR(pDI, IDirectInput8), NULL);
	if ( FAILED(hRes) )
	{
		// DirectInput not available; take appropriate action 
		LOG_MSG( "GetDIHookOffsets: DirectInput8Create failed. 0x%08x" LOG_CR, hRes );
		return false;
	}

	hRes = pDI->CreateDevice(GUID_SysKeyboard, IREF_GETPPTR(pDevice, IDirectInputDevice8), NULL);
	if ( FAILED(hRes) )
	{
		LOG_MSG( "GetDIHookOffsets: IDirectInput8::CreateDevice() FAILED. 0x%08x" LOG_CR, hRes );
		return false;
	}

	UINT_PTR* pVTable = (UINT_PTR*)(*((UINT_PTR*)pDevice.get_RefObj()));
	s_nDI8_GetDeviceState = ( pVTable[DI8_DEVICE_GetDeviceState] - dll.get_DllInt());
	//LOG_MSG( "GetDIHookOffsets: dll base 0x%08x GetDeviceState offset 0x%08x\n", dll.get_DllInt( ), s_nDI8_GetDeviceState );
	s_nDI8_GetDeviceData = ( pVTable[DI8_DEVICE_GetDeviceData] - dll.get_DllInt());
	//LOG_MSG( "GetDIHookOffsets: dll base 0x%08x GetDeviceData offset 0x%08x\n", dll.get_DllInt( ), s_nDI8_GetDeviceData );	

	return true;
}

HRESULT WINAPI DI8_GetDeviceState( IDirectInputDevice8 *pDevice, DWORD dwSize, LPVOID lpState )
{
	s_HookDeviceState.SwapOld( s_DI8_GetDeviceState );

	// do our preliminary shit here - store device pointer, etc
	//LOG_MSG( "DI8_GetDeviceState: called for device 0x%08x"LOG_CR, pDevice );

	HRESULT hRes = s_DI8_GetDeviceState( pDevice, dwSize, lpState );

	// do our shit here, eyeballing and messing with the input

	s_HookDeviceState.SwapReset( s_DI8_GetDeviceState );
	
	return hRes;
}

HRESULT WINAPI DI8_GetDeviceData( IDirectInputDevice8 *pDevice, DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags )
{
	s_HookDeviceData.SwapOld( s_DI8_GetDeviceState );

	// do our preliminary shit here - store device pointer, etc
	//LOG_MSG( "DI8_GetDeviceData: called for device 0x%08x"LOG_CR, pDevice );

	HRESULT hRes = s_DI8_GetDeviceData( pDevice, cbObjectData, rgdod, pdwInOut, dwFlags );

	// do our shit here, eyeballing and messing with the input

	s_HookDeviceData.SwapReset( s_DI8_GetDeviceState );

	return hRes;
}

bool HookDI( UINT_PTR dll_base )
{
	if ( !s_nDI8_GetDeviceState || !s_nDI8_GetDeviceData )
		return true;

	LOG_MSG( "DI8:HookFunctions: dinput8.dll loaded at 0x%08x" LOG_CR, dll_base );
	s_DI8_GetDeviceState = (GETDEVICESTATE)(dll_base + s_nDI8_GetDeviceState);
	if ( !s_HookDeviceState.InstallHook(s_DI8_GetDeviceState, DI8_GetDeviceState) )
	{
		LOG_MSG( "DI8:HookFunctions: unable to hook DirectInput function GetDeviceState" LOG_CR );
		//return false;
	}

	s_DI8_GetDeviceData = (GETDEVICEDATA)(dll_base + s_nDI8_GetDeviceData);
	if ( !s_HookDeviceData.InstallHook(s_DI8_GetDeviceData, DI8_GetDeviceData) )
	{
		LOG_MSG( "DI8:HookFunctions: unable to hook DirectInput function GetDeviceData" LOG_CR );
		//return false;
	}

	return true;
}

void UnhookDI( void )
{
	s_HookDeviceState.RemoveHook( s_DI8_GetDeviceState );
	s_HookDeviceData.RemoveHook( s_DI8_GetDeviceData );
}
#endif

BOOL WINAPI Hook_GetKeyboardState( PBYTE lpKeyState )
{
	s_HookGetKeyboardState.SwapOld( s_GetKeyboardState );
	//LOG_MSG( "Hook_GetKeyboardState: called"LOG_CR );
	BOOL res = s_GetKeyboardState( lpKeyState );
	// update our saved key states to generate events. can also modify the provided state if we're showing overlay (to hide input from the game)
	UpdateKeyboardState( lpKeyState );
	s_HookGetKeyboardState.SwapReset( s_GetKeyboardState );
	return res;
}

SHORT WINAPI Hook_GetAsyncKeyState( int vKey )
{
	s_HookGetAsyncKeyState.SwapOld( s_GetAsyncKeyState );
	//LOG_MSG( "Hook_GetAsyncKeyState: called for key %u"LOG_CR, vKey );
	SHORT res = s_GetAsyncKeyState( vKey );
	res = UpdateSingleKeyState( vKey, res );
	// mess with input here if we're in overlay mode
	s_HookGetAsyncKeyState.SwapReset( s_GetAsyncKeyState );
	return res;
}

BOOL WINAPI Hook_GetCursorPos( LPPOINT lpPoint )
{
	s_HookGetCursorPos.SwapOld( s_GetCursorPos );
	BOOL res = s_GetCursorPos( lpPoint );
	// mess with it here
	//LOG_MSG( "Hook_GetCursorPos: current pos is [%u, %u]"LOG_CR, lpPoint->x, lpPoint->y );
	s_HookGetCursorPos.SwapReset( s_GetCursorPos );
	return res;
}

BOOL WINAPI Hook_SetCursorPos( INT x, INT y )
{
	s_HookSetCursorPos.SwapOld( s_SetCursorPos );
	BOOL res = s_SetCursorPos( x, y );
	// mess with it here
	//LOG_MSG( "Hook_SetCursorPos: setting pos to [%u, %u]"LOG_CR, x, y );
	s_HookSetCursorPos.SwapReset( s_SetCursorPos );
	return res;
}

UINT WINAPI Hook_GetRawInputData( HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader )
{
	s_HookGetRawInputData.SwapOld( s_GetRawInputData );
	UINT res = s_GetRawInputData( hRawInput, uiCommand, pData, pcbSize, cbSizeHeader );
	//LOG_MSG( "Hook_GetRawInputData: called with command %u"LOG_CR, uiCommand );
	if ( pData ) // called with pData == NULL means they're just asking for a size to allocate
	{
		RAWINPUT* raw = (RAWINPUT*)pData;
		if ( raw->header.dwType == RIM_TYPEKEYBOARD )
			UpdateRawKeyState( &(raw->data.keyboard) );
	}
	s_HookGetRawInputData.SwapReset( s_GetRawInputData );
	return res;
}

UINT WINAPI Hook_GetRawInputBuffer( PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader )
{
	s_HookGetRawInputBuffer.SwapOld( s_GetRawInputBuffer );
	UINT res = s_GetRawInputBuffer( pData, pcbSize, cbSizeHeader );
	//LOG_MSG( "Hook_GetRawInputBuffer: called"LOG_CR );
	s_HookGetRawInputBuffer.SwapReset( s_GetRawInputBuffer );
	return res;
}

bool HookInput( void )
{
	HMODULE dll = GetModuleHandle( L"user32.dll" );
	s_GetKeyboardState = (GETKEYBOARDSTATE)GetProcAddress( dll, "GetKeyboardState" );
	if ( !s_HookGetKeyboardState.InstallHook( s_GetKeyboardState, Hook_GetKeyboardState ) )
	{
		LOG_MSG( "HookInput: unable to hook function GetKeyboardState" LOG_CR );
		return false;
	}

	s_GetAsyncKeyState = (GETASYNCKEYSTATE)GetProcAddress( dll, "GetAsyncKeyState" );
	if ( !s_HookGetAsyncKeyState.InstallHook( s_GetAsyncKeyState, Hook_GetAsyncKeyState ) )
	{
		LOG_MSG( "HookInput: unable to hook function GetAsyncKeyState" LOG_CR );
		return false;
	}

	s_GetCursorPos = (GETCURSORPOS)GetProcAddress( dll, "GetCursorPos" );
	if ( !s_HookGetCursorPos.InstallHook( s_GetCursorPos, Hook_GetCursorPos ) )
	{
		LOG_MSG( "HookInput: unable to hook function GetCursorPos" LOG_CR );
		return false;
	}

	s_SetCursorPos = (SETCURSORPOS)GetProcAddress( dll, "SetCursorPos" );
	if ( !s_HookSetCursorPos.InstallHook( s_SetCursorPos, Hook_SetCursorPos ) )
	{
		LOG_MSG( "HookInput: unable to hook function SetCursorPos" LOG_CR );
		return false;
	}

	s_GetRawInputData = (GETRAWINPUTDATA)GetProcAddress( dll, "GetRawInputData" );
	if ( !s_HookGetRawInputData.InstallHook( s_GetRawInputData, Hook_GetRawInputData ) )
	{
		LOG_MSG( "HookInput: unable to hook function GetRawInputData" LOG_CR );
		return false;
	}

	s_GetRawInputBuffer = (GETRAWINPUTBUFFER)GetProcAddress( dll, "GetRawInputBuffer" );
	if ( !s_HookGetRawInputBuffer.InstallHook( s_GetRawInputBuffer, Hook_GetRawInputBuffer ) )
	{
		LOG_MSG( "HookInput: unable to hook function GetRawInputBuffer" LOG_CR );
		return false;
	}
	return true;
}

void UnhookInput( void )
{
	s_HookGetKeyboardState.RemoveHook( s_GetKeyboardState );
	s_HookGetAsyncKeyState.RemoveHook( s_GetAsyncKeyState );
	s_HookGetCursorPos.RemoveHook( s_GetCursorPos );
	s_HookSetCursorPos.RemoveHook( s_SetCursorPos );
	s_HookGetRawInputData.RemoveHook( s_GetRawInputData );
	s_HookGetRawInputBuffer.RemoveHook( s_GetRawInputBuffer );
}

// handle any input events sent to game's window. return true if we're eating them (ie: showing overlay)
// we should try to keep this code simple and pass messages off to appropriate handler functions.
bool InputWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch ( uMsg )
	{
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			return UpdateWMKeyState( wParam, KEY_DOWN );
		case WM_KEYUP:
		case WM_SYSKEYUP:
			return UpdateWMKeyState( wParam, KEY_UP );
		case WM_CHAR:
			return UpdateWMKeyState( wParam, KEY_CHAR );
	}

	return false;
}