// Context menu handler for Open-Shell Modern Settings shell folder

// Based on Explorer Data Provider Sample (https://docs.microsoft.com/en-us/windows/win32/shell/samples-explorerdataprovider)

#include "stdafx.h"
#include "ModernSettings.h"
#include "ModernSettingsContextMenu.h"
#include "ComHelper.h"

#define MENUVERB_OPEN     0

struct ICIVERBTOIDMAP
{
	LPCWSTR pszCmd;         // verbW
	LPCSTR  pszCmdA;        // verbA
	UINT    idCmd;          // hmenu id
};

static const ICIVERBTOIDMAP g_ContextMenuIDMap[] =
{
	{ L"open",       "open",      MENUVERB_OPEN },
};

HRESULT _MapICIVerbToCmdID(LPCMINVOKECOMMANDINFO pici, UINT* pid)
{
	if (IS_INTRESOURCE(pici->lpVerb))
	{
		*pid = LOWORD((UINT_PTR)pici->lpVerb);
		return S_OK;
	}

	if (pici->fMask & CMIC_MASK_UNICODE)
	{
		for (const auto& i : g_ContextMenuIDMap)
		{
			if (StrCmpIC(((LPCMINVOKECOMMANDINFOEX)pici)->lpVerbW, i.pszCmd) == 0)
			{
				*pid = i.idCmd;
				return S_OK;
			}
		}
	}
	else
	{
		for (const auto& i : g_ContextMenuIDMap)
		{
			if (StrCmpICA(pici->lpVerb, i.pszCmdA) == 0)
			{
				*pid = i.idCmd;
				return S_OK;
			}
		}
	}

	return E_FAIL;
}

static bool ActivateModernSettingPage(const WCHAR* page)
{
	CComPtr<IApplicationActivationManager> mgr;
	mgr.CoCreateInstance(CLSID_ApplicationActivationManager);
	if (mgr)
	{
		DWORD pid = 0;
		return SUCCEEDED(mgr->ActivateApplication(L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel", page, AO_NONE, &pid));
	}

	return false;
}

extern ModernSettings::Setting GetModernSetting(LPCITEMIDLIST pidl);

static HRESULT Execute(const wchar_t* cmd)
{
	return (intptr_t)::ShellExecute(nullptr, L"open", cmd, nullptr, nullptr, SW_SHOWNORMAL) > 32 ? S_OK : E_FAIL;
}

static HRESULT ExecuteCommandWithShell(const wchar_t* command)
{
	using EvaluateCommandTemplate = HRESULT (WINAPI*)(PCWSTR, PWSTR*, PWSTR*, PWSTR*);
	auto evaluate = reinterpret_cast<EvaluateCommandTemplate>(
		GetProcAddress(GetModuleHandle(L"shell32.dll"), "SHEvaluateSystemCommandTemplate"));
	if (!evaluate)
		return HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED);

	CComHeapPtr<wchar_t> application;
	CComHeapPtr<wchar_t> arguments;
	HRESULT hr = evaluate(command, &application, nullptr, &arguments);
	if (FAILED(hr))
		return hr;

	SHELLEXECUTEINFO execute = { sizeof(execute) };
	execute.lpVerb = L"open";
	execute.lpFile = application;
	execute.lpParameters = arguments;
	execute.nShow = SW_SHOWNORMAL;
	return ShellExecuteEx(&execute) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

static HRESULT OpenItemByPidl(LPCITEMIDLIST pidl)
{
	auto child = ILFindLastID(pidl);
	auto setting = GetModernSetting(child);

	if (!setting)
		return E_INVALIDARG;

	if (setting.hostId == L"{6E6DDBCB-9C89-434B-A994-D5F22239523B}")
	{
		if (setting.deepLink.empty())
			return E_INVALIDARG;

		std::wstring cmd(L"windowsdefender://");
		cmd += setting.deepLink;

		return Execute(cmd.c_str());
	}

	if (setting.hostId == L"{7E0522FC-1AC4-41CA-AFD0-3610417A9C41}")
	{
		if (setting.pageId.empty())
			return E_INVALIDARG;

		std::wstring cmd(L"shell:::");
		cmd += setting.pageId;

		return Execute(cmd.c_str());
	}

	if (setting.hostId == L"{12B1697E-D3A0-4DBC-B568-CCF64A3F934D}")
	{
		if (setting.deepLink.empty())
			return E_INVALIDARG;

		std::wstring cmd(setting.deepLink);

		if (cmd.compare(0, 8, L"shell:::") == 0)
			return Execute(cmd.c_str());

		cmd.resize(MAX_PATH);
		DoEnvironmentSubst(cmd.data(), (UINT)cmd.size());

		STARTUPINFO startupInfo = { sizeof(startupInfo) };
		PROCESS_INFORMATION processInfo{};

		// CreateProcess can modify its input, so keep the command for the ShellExecute fallback.
		std::wstring processCommand(cmd);
		if (!CreateProcess(nullptr, processCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo))
		{
			const DWORD error = GetLastError();
			if (error == ERROR_ELEVATION_REQUIRED)
				return ExecuteCommandWithShell(cmd.c_str());
			return HRESULT_FROM_WIN32(error);
		}

		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);

		return S_OK;
	}

	if (setting.pageId.empty())
		return E_INVALIDARG;

	std::wstring page;

	page += L"page=";
	page += setting.pageId;

	if (!setting.settingId.empty())
	{
		page += L"&target=";
		page += setting.settingId;
	}
	else if (!setting.groupId.empty())
	{
		page += L"&group=";
		page += setting.groupId;
	}

	page += L"&ActivationType=Search";

	ActivateModernSettingPage(page.c_str());

	return S_OK;
}


// CModernSettingsContextMenu

HRESULT CModernSettingsContextMenu::QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT /* idCmdLast */, UINT /* uFlags */)
{
	InsertMenu(hmenu, indexMenu++, MF_BYPOSITION, idCmdFirst + MENUVERB_OPEN, L"Open");
	// other verbs could go here...

	// indicate that we added one verb.
	return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)(1));
}

HRESULT CModernSettingsContextMenu::InvokeCommand(LPCMINVOKECOMMANDINFO pici)
{
	HRESULT hr = E_INVALIDARG;
	UINT uID;
	// Is this command for us?
	if (SUCCEEDED(_MapICIVerbToCmdID(pici, &uID)))
	{
		if (uID == MENUVERB_OPEN && m_pdtobj)
		{
			CAbsolutePidl pidl;
			hr = SHGetIDListFromObject(m_pdtobj, &pidl);
			if (SUCCEEDED(hr))
				hr = OpenItemByPidl(pidl);
		}
	}

	return hr;
}

HRESULT CModernSettingsContextMenu::GetCommandString(UINT_PTR /* idCmd */, UINT /* uType */, UINT* /* pRes */, LPSTR /* pszName */, UINT /* cchMax */)
{
	return E_NOTIMPL;
}

HRESULT CModernSettingsContextMenu::Initialize(PCIDLIST_ABSOLUTE /* pidlFolder */, IDataObject* pdtobj, HKEY /* hkeyProgID */)
{
	m_pdtobj = pdtobj;
	return S_OK;
}

HRESULT CModernSettingsContextMenu::SetSite(IUnknown* punkSite)
{
	m_punkSite = punkSite;
	return S_OK;
}

HRESULT CModernSettingsContextMenu::GetSite(REFIID riid, void** ppvSite)
{
	return m_punkSite ? m_punkSite->QueryInterface(riid, ppvSite) : E_FAIL;
}
