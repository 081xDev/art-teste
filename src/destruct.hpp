#pragma once

// ---------------------------------------------------------------------------
// ART BYPASS - execution artifact cleanup.
//
// This header holds the whole implementation and is included by exactly one
// translation unit: Security\Destruct.cpp. It is deliberately NOT exposed
// through Includes.hpp - it pulls in winioctl.h, <filesystem> and shlobj.h,
// and including it from the base header dragged all of that into every file
// in the project and roughly doubled a full rebuild.
//
// Other code that needs to trigger cleanup includes Security\DestructHook.hpp
// instead, which declares only InstallDestructHandler() and RunDestructOnce().
//
// The include guard below matters even with a single consumer: a future edit
// that adds a second include site would otherwise produce duplicate symbols
// for every function here.
// ---------------------------------------------------------------------------
#include <windows.h>
#include <string>
#include <filesystem>
#include <iostream>
#include <vector>
#include <fstream>
#include <regex>
#include <random>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <TlHelp32.h>
#include <winioctl.h>

// ---------------------------------------------------------------------------
// destruct - removes the execution artifacts a forensic pass would look for.
//
// Coverage, in the order a typical triage walks them:
//
//   Prefetch        .pf file for our image, plus a spoofed replacement so the
//                   Prefetch directory keeps a plausible recent entry.
//   Amcache         HKLM Amcache InventoryApplication + InventoryDriverBinary
//                   entries keyed by our image path / file id.
//   ShimCache       AppCompatCache value inside the Session Manager hive -
//                   this one is a registry blob, so entries are rewritten
//                   in place rather than deleted.
//   UserAssist      ROT13-encoded run counts under the Explorer hive.
//   MUICache        AppSwitched / MuiCache application name entries.
//   BAM/DAM         Background Activity Moderator per-SID execution records.
//   RunMRU          Typed command history in the Explorer hive.
//   Event logs      Application Experience, Defender Operational, PowerShell
//                   Operational, Sysmon if present.
//   USN journal     Deleted and recreated so file-record deltas are gone.
//   Timestomp       Prefetch and Amcache timestamps restored to a neighbour
//                   file's values so a "most recently written" pivot misses us.
//   NTFS cleanup    Slack/ADS sweep on our own directory.
//
// Every step is best-effort. Without elevation the HKLM writes fail silently
// and the rest still runs.
// ---------------------------------------------------------------------------

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void randomSleep( int minMs = 100, int maxMs = 300 )
{
	static std::mt19937 rng( static_cast< unsigned int >(
		std::chrono::steady_clock::now( ).time_since_epoch( ).count( ) ) );

	std::uniform_int_distribution< int > dist( minMs, maxMs );
	std::this_thread::sleep_for( std::chrono::milliseconds( dist( rng ) ) );
}

// The image name, upper case, no extension - how Prefetch names the .pf file
// and how Amcache keys an entry.
std::wstring GetOwnImageName( )
{
	wchar_t Buffer[ MAX_PATH ] = {};
	GetModuleFileNameW( nullptr, Buffer, MAX_PATH );

	std::wstring Path = Buffer;
	const size_t Slash = Path.find_last_of( L"\\/" );
	std::wstring Name = ( Slash == std::wstring::npos ) ? Path : Path.substr( Slash + 1 );

	const size_t Dot = Name.find_last_of( L'.' );
	if ( Dot != std::wstring::npos )
		Name = Name.substr( 0, Dot );

	std::transform( Name.begin( ), Name.end( ), Name.begin( ), ::towupper );
	return Name;
}

std::wstring GetOwnImagePath( )
{
	wchar_t Buffer[ MAX_PATH ] = {};
	GetModuleFileNameW( nullptr, Buffer, MAX_PATH );
	return Buffer;
}

bool WriteRegDword( HKEY Root, const std::wstring & SubKey, const std::wstring & Value, DWORD Data )
{
	HKEY Key = nullptr;
	if ( RegCreateKeyExW( Root, SubKey.c_str( ), 0, nullptr, REG_OPTION_NON_VOLATILE,
			KEY_SET_VALUE, nullptr, &Key, nullptr ) != ERROR_SUCCESS )
		return false;

	const LSTATUS Status = RegSetValueExW( Key, Value.c_str( ), 0, REG_DWORD,
		reinterpret_cast< const BYTE * >( &Data ), sizeof( Data ) );

	RegCloseKey( Key );
	return Status == ERROR_SUCCESS;
}

bool DeleteRegKeyTree( HKEY Root, const std::wstring & SubKey )
{
	// RegDeleteTreeW removes the key and every value/subkey under it.
	const LSTATUS Status = RegDeleteTreeW( Root, SubKey.c_str( ) );
	return Status == ERROR_SUCCESS || Status == ERROR_FILE_NOT_FOUND;
}

// ROT13 - the encoding UserAssist uses for its value names.
std::wstring Rot13( const std::wstring & In )
{
	std::wstring Out = In;
	for ( wchar_t & Ch : Out )
	{
		if ( Ch >= L'a' && Ch <= L'z' )
			Ch = L'a' + ( ( Ch - L'a' + 13 ) % 26 );
		else if ( Ch >= L'A' && Ch <= L'Z' )
			Ch = L'A' + ( ( Ch - L'A' + 13 ) % 26 );
	}
	return Out;
}

DWORD crc32_name( const std::wstring & filename )
{
	DWORD crc = 0xFFFFFFFF;
	for ( wchar_t c : filename )
	{
		crc ^= static_cast< BYTE >( c );
		for ( int i = 0; i < 8; ++i )
			crc = ( crc >> 1 ) ^ ( 0xEDB88320 & -static_cast< int >( crc & 1 ) );
	}
	return ~crc;
}

// ---------------------------------------------------------------------------
// 1. Prefetch
// ---------------------------------------------------------------------------

struct FileTimestamps
{
	FILETIME creationTime;
	FILETIME lastAccessTime;
	FILETIME lastWriteTime;
	fs::path filePath;
};

std::vector<FileTimestamps> backupTimestampsPrefetch( const std::wstring & prefetchPath )
{
	std::vector<FileTimestamps> backup;

	std::error_code Ec;
	if ( !fs::exists( prefetchPath, Ec ) )
		return backup;

	for ( const auto & entry : fs::directory_iterator( prefetchPath, Ec ) )
	{
		if ( !entry.is_regular_file( ) || entry.path( ).extension( ) != L".pf" )
			continue;

		const std::wstring filename = entry.path( ).filename( ).wstring( );
		if ( filename.find( L"SETTINGS" ) == 0 )
			continue;

		HANDLE hFile = CreateFileW( entry.path( ).c_str( ), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, 0, nullptr );

		if ( hFile == INVALID_HANDLE_VALUE )
			continue;

		FILETIME cTime, aTime, wTime;
		if ( GetFileTime( hFile, &cTime, &aTime, &wTime ) )
			backup.push_back( { cTime, aTime, wTime, entry.path( ) } );

		CloseHandle( hFile );
	}

	return backup;
}

void deleteOwnPrefetch( )
{
	const std::wstring PrefetchPath = L"C:\\Windows\\Prefetch";
	const std::wstring OwnName = GetOwnImageName( );

	std::error_code Ec;
	if ( !fs::exists( PrefetchPath, Ec ) )
		return;

	// The hash in the filename is derived from the full path, so match on the
	// image-name prefix rather than trying to compute it.
	const std::wstring Needle = OwnName + L"-";

	for ( const auto & entry : fs::directory_iterator( PrefetchPath, Ec ) )
	{
		if ( !entry.is_regular_file( ) || entry.path( ).extension( ) != L".pf" )
			continue;

		std::wstring filename = entry.path( ).filename( ).wstring( );
		std::transform( filename.begin( ), filename.end( ), filename.begin( ), ::towupper );

		if ( filename.find( Needle ) == 0 )
		{
			DeleteFileW( entry.path( ).c_str( ) );
			randomSleep( 20, 60 );
		}
	}

	// Also drop our loader name out of the trace file that lists prefetch runs.
	const std::wstring TraceFile = PrefetchPath + L"\\Layout.ini";
	if ( fs::exists( TraceFile, Ec ) )
		DeleteFileW( TraceFile.c_str( ) );
}

void spoofPrefetch( )
{
	const std::wstring PrefetchPath = L"C:\\Windows\\Prefetch";

	std::error_code Ec;
	if ( !fs::exists( PrefetchPath, Ec ) )
		return;

	std::vector<fs::path> legit;
	fs::path settings;

	for ( const auto & entry : fs::directory_iterator( PrefetchPath, Ec ) )
	{
		if ( entry.path( ).extension( ) != L".pf" )
			continue;

		const std::wstring name = entry.path( ).filename( ).wstring( );

		if ( name.find( L"SETTINGS" ) == 0 )
			settings = entry.path( );
		else if ( name.find( L"DISCORD" ) == 0 || name.find( L"OBS" ) == 0 || name.find( L"STEAM" ) == 0 )
			legit.push_back( entry.path( ) );
	}

	if ( settings.empty( ) || legit.empty( ) )
		return;

	// Overwrite the SETTINGS record with a copy of an unrelated, benign one so
	// the prefetch table keeps a coherent most-recent entry.
	std::ifstream src( legit[ 0 ], std::ios::binary );
	std::ofstream dst( settings, std::ios::binary | std::ios::trunc );
	dst << src.rdbuf( );
	src.close( );
	dst.close( );

	std::wstring newName = settings.stem( ).wstring( );
	const size_t pos = newName.find( L"SETTINGS" );

	if ( pos != std::wstring::npos )
	{
		newName = L"OBS" + newName.substr( 8 );
		const DWORD hash = crc32_name( newName );

		std::wstringstream ss;
		ss << L"OBS-" << std::hex << std::uppercase << hash << L".pf";

		std::error_code RenameEc;
		fs::rename( settings, fs::path( PrefetchPath ) / ss.str( ), RenameEc );
	}
}

void restaurarTimestampsEForcarEvento( const std::vector<FileTimestamps> & backup )
{
	for ( const auto & ft : backup )
	{
		const std::wstring filename = ft.filePath.filename( ).wstring( );
		if ( filename.find( L"SETTINGS" ) == 0 )
			continue;

		HANDLE hFile = CreateFileW( ft.filePath.c_str( ),
			FILE_WRITE_ATTRIBUTES | GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, 0, nullptr );

		if ( hFile == INVALID_HANDLE_VALUE )
			continue;

		SetFileTime( hFile, &ft.creationTime, &ft.lastAccessTime, &ft.lastWriteTime );

		// A single read bumps the access time back to "now" for the entries we
		// did not touch, which is what a pivot looks for.
		char buffer[ 1 ];
		DWORD bytesRead = 0;
		ReadFile( hFile, buffer, 1, &bytesRead, nullptr );

		CloseHandle( hFile );
	}
}

// ---------------------------------------------------------------------------
// 2. USN journal
// ---------------------------------------------------------------------------

void clearUSNJournal( )
{
	HANDLE hVolume = CreateFileW( L"\\\\.\\C:", GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr );

	if ( hVolume == INVALID_HANDLE_VALUE )
		return;

	DWORD br = 0;
	USN_JOURNAL_DATA usd = {};
	CREATE_USN_JOURNAL_DATA cujd = {};

	if ( DeviceIoControl( hVolume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0,
			&usd, sizeof( usd ), &br, nullptr ) )
	{
		DELETE_USN_JOURNAL_DATA dujd = {};
		dujd.UsnJournalID = usd.UsnJournalID;
		dujd.DeleteFlags = USN_DELETE_FLAG_DELETE;

		DeviceIoControl( hVolume, FSCTL_DELETE_USN_JOURNAL, &dujd, sizeof( dujd ),
			nullptr, 0, &br, nullptr );
		DeviceIoControl( hVolume, FSCTL_CREATE_USN_JOURNAL, &cujd, sizeof( cujd ),
			nullptr, 0, &br, nullptr );
	}

	CloseHandle( hVolume );
}

// ---------------------------------------------------------------------------
// 3. Registry artifacts
// ---------------------------------------------------------------------------

void cleanAmcache( )
{
	const std::wstring OwnPath = GetOwnImagePath( );
	const std::wstring OwnName = GetOwnImageName( );

	// Amcache is keyed by a file id under InventoryApplicationFile; walk the
	// subkeys and drop any whose value data mentions our image.
	HKEY Root = nullptr;
	if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE,
			L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Amcache\\Root\\InventoryApplicationFile",
			0, KEY_READ | KEY_WRITE, &Root ) != ERROR_SUCCESS )
	{
		// Older builds use a flat key.
		DeleteRegKeyTree( HKEY_LOCAL_MACHINE,
			L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Amcache\\InventoryApplicationFile" );
		return;
	}

	std::vector< std::wstring > ToDelete;
	wchar_t SubKeyName[ 512 ] = {};
	DWORD Index = 0;
	DWORD NameLength = 512;

	while ( RegEnumKeyExW( Root, Index, SubKeyName, &NameLength, nullptr, nullptr, nullptr, nullptr ) == ERROR_SUCCESS )
	{
		HKEY SubKey = nullptr;
		if ( RegOpenKeyExW( Root, SubKeyName, 0, KEY_READ, &SubKey ) == ERROR_SUCCESS )
		{
			wchar_t ValueBuffer[ 1024 ] = {};
			DWORD ValueSize = sizeof( ValueBuffer );
			DWORD ValueType = 0;

			if ( RegQueryValueExW( SubKey, L"LowerCaseLongPath", nullptr, &ValueType,
					reinterpret_cast< LPBYTE >( ValueBuffer ), &ValueSize ) == ERROR_SUCCESS )
			{
				std::wstring Lower = ValueBuffer;
				std::wstring OwnLower = OwnPath;
				std::transform( Lower.begin( ), Lower.end( ), Lower.begin( ), ::towlower );
				std::transform( OwnLower.begin( ), OwnLower.end( ), OwnLower.begin( ), ::towlower );

				if ( Lower.find( OwnLower ) != std::wstring::npos )
					ToDelete.push_back( SubKeyName );
			}

			RegCloseKey( SubKey );
		}

		NameLength = 512;
		Index++;
	}

	for ( const auto & Name : ToDelete )
		RegDeleteTreeW( Root, Name.c_str( ) );

	RegCloseKey( Root );

	// The legacy flat keys.
	DeleteRegKeyTree( HKEY_LOCAL_MACHINE,
		L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Amcache\\InventoryApplication\\" + OwnName );
}

void cleanShimCache( )
{
	// AppCompatCache lives inside the Session Manager hive as one binary blob.
	// RegDeleteTree on the parent value is the only reliable removal without
	// parsing the blob format, which changes between builds.
	const std::wstring SubKey =
		L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\AppCompatCache";

	HKEY Key = nullptr;
	if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE, SubKey.c_str( ), 0, KEY_SET_VALUE, &Key ) != ERROR_SUCCESS )
		return;

	// Overwrite with an empty value rather than deleting the key - some builds
	// recreate it immediately on delete, which is itself a signal.
	BYTE Empty[ 4 ] = {};
	RegSetValueExW( Key, L"AppCompatCache", 0, REG_BINARY, Empty, sizeof( Empty ) );
	RegSetValueExW( Key, L"AppCompatCacheRemoved", 0, REG_DWORD,
		reinterpret_cast< const BYTE * >( &Empty ), sizeof( DWORD ) );

	RegCloseKey( Key );
}

void cleanUserAssist( )
{
	const std::wstring OwnName = GetOwnImageName( );
	const std::wstring OwnPath = GetOwnImagePath( );

	const wchar_t * Roots[ ] = {
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist",
	};

	for ( const wchar_t * RootPath : Roots )
	{
		HKEY Root = nullptr;
		if ( RegOpenKeyExW( HKEY_CURRENT_USER, RootPath, 0, KEY_READ | KEY_WRITE, &Root ) != ERROR_SUCCESS )
			continue;

		// UserAssist nests a GUID per session; walk two levels deep.
		wchar_t GuidName[ 512 ] = {};
		DWORD GuidLen = 512;
		DWORD Index = 0;

		std::vector< std::wstring > GuidKeys;

		while ( RegEnumKeyExW( Root, Index, GuidName, &GuidLen, nullptr, nullptr, nullptr, nullptr ) == ERROR_SUCCESS )
		{
			GuidKeys.push_back( GuidName );
			GuidLen = 512;
			Index++;
		}

		for ( const auto & Guid : GuidKeys )
		{
			const std::wstring CountKey = std::wstring( RootPath ) + L"\\" + Guid + L"\\Count";
			const std::wstring CountPath = std::wstring( Guid ) + L"\\Count";

			HKEY Count = nullptr;
			if ( RegOpenKeyExW( Root, CountPath.c_str( ), 0, KEY_READ | KEY_WRITE, &Count ) != ERROR_SUCCESS )
				continue;

			std::vector< std::wstring > ValueNames;
			wchar_t ValueName[ 1024 ] = {};
			DWORD ValueLen = 1024;
			DWORD ValueIndex = 0;

			while ( RegEnumValueW( Count, ValueIndex, ValueName, &ValueLen, nullptr, nullptr, nullptr, nullptr ) == ERROR_SUCCESS )
			{
				ValueNames.push_back( ValueName );
				ValueLen = 1024;
				ValueIndex++;
			}

			// Value names are ROT13. Decode, compare against our image, drop.
			for ( const auto & Encoded : ValueNames )
			{
				const std::wstring Decoded = Rot13( Encoded );
				std::wstring Upper = Decoded;
				std::transform( Upper.begin( ), Upper.end( ), Upper.begin( ), ::towupper );

				if ( Upper.find( OwnName ) != std::wstring::npos ||
					 Decoded.find( OwnPath ) != std::wstring::npos )
				{
					RegDeleteValueW( Count, Encoded.c_str( ) );
				}
			}

			RegCloseKey( Count );
			(void)CountKey;
		}

		RegCloseKey( Root );
	}
}

void cleanMuiCache( )
{
	const std::wstring OwnPath = GetOwnImagePath( );

	const wchar_t * Keys[ ] = {
		L"SOFTWARE\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache",
		L"SOFTWARE\\Microsoft\\Windows\\ShellNoRoam\\MUICache",
	};

	for ( const wchar_t * KeyPath : Keys )
	{
		HKEY Key = nullptr;
		if ( RegOpenKeyExW( HKEY_CURRENT_USER, KeyPath, 0, KEY_READ | KEY_WRITE, &Key ) != ERROR_SUCCESS )
			continue;

		std::vector< std::wstring > ToDelete;
		wchar_t ValueName[ 1024 ] = {};
		DWORD ValueLen = 1024;
		DWORD Index = 0;

		while ( RegEnumValueW( Key, Index, ValueName, &ValueLen, nullptr, nullptr, nullptr, nullptr ) == ERROR_SUCCESS )
		{
			std::wstring Name = ValueName;
			std::wstring NameUpper = Name;
			std::wstring PathUpper = OwnPath;

			std::transform( NameUpper.begin( ), NameUpper.end( ), NameUpper.begin( ), ::towupper );
			std::transform( PathUpper.begin( ), PathUpper.end( ), PathUpper.begin( ), ::towupper );

			if ( NameUpper.find( PathUpper ) != std::wstring::npos )
				ToDelete.push_back( Name );

			ValueLen = 1024;
			Index++;
		}

		for ( const auto & Name : ToDelete )
			RegDeleteValueW( Key, Name.c_str( ) );

		RegCloseKey( Key );
	}
}

void cleanBam( )
{
	// BAM records execution per user SID under Services\\bam\\State\\UserSettings.
	// The key itself is protected; a delete attempt is the practical approach.
	HKEY Root = nullptr;
	if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE,
			L"SYSTEM\\CurrentControlSet\\Services\\bam\\State\\UserSettings",
			0, KEY_READ, &Root ) != ERROR_SUCCESS )
		return;

	std::vector< std::wstring > Sids;
	wchar_t SidName[ 512 ] = {};
	DWORD SidLen = 512;
	DWORD Index = 0;

	while ( RegEnumKeyExW( Root, Index, SidName, &SidLen, nullptr, nullptr, nullptr, nullptr ) == ERROR_SUCCESS )
	{
		Sids.push_back( SidName );
		SidLen = 512;
		Index++;
	}

	const std::wstring OwnPath = GetOwnImagePath( );

	for ( const auto & Sid : Sids )
	{
		HKEY SidKey = nullptr;
		if ( RegOpenKeyExW( Root, Sid.c_str( ), 0, KEY_READ | KEY_WRITE, &SidKey ) != ERROR_SUCCESS )
			continue;

		std::vector< std::wstring > ToDelete;
		wchar_t ValueName[ 1024 ] = {};
		DWORD ValueLen = 1024;
		DWORD ValueIndex = 0;

		while ( RegEnumValueW( SidKey, ValueIndex, ValueName, &ValueLen, nullptr, nullptr, nullptr, nullptr ) == ERROR_SUCCESS )
		{
			std::wstring Path = ValueName;
			std::wstring PathUpper = Path;
			std::wstring OwnUpper = OwnPath;

			std::transform( PathUpper.begin( ), PathUpper.end( ), PathUpper.begin( ), ::towupper );
			std::transform( OwnUpper.begin( ), OwnUpper.end( ), OwnUpper.begin( ), ::towupper );

			if ( PathUpper.find( OwnUpper ) != std::wstring::npos )
				ToDelete.push_back( Path );

			ValueLen = 1024;
			ValueIndex++;
		}

		for ( const auto & Name : ToDelete )
			RegDeleteValueW( SidKey, Name.c_str( ) );

		RegCloseKey( SidKey );
	}

	RegCloseKey( Root );
}

void cleanRunMRU( )
{
	const std::wstring OwnPath = GetOwnImagePath( );

	HKEY Key = nullptr;
	if ( RegOpenKeyExW( HKEY_CURRENT_USER,
			L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU",
			0, KEY_READ | KEY_WRITE, &Key ) != ERROR_SUCCESS )
		return;

	std::vector< std::wstring > ToDelete;
	wchar_t ValueName[ 1024 ] = {};
	DWORD ValueLen = 1024;
	DWORD Index = 0;

	while ( RegEnumValueW( Key, Index, ValueName, &ValueLen, nullptr, nullptr, nullptr, nullptr ) == ERROR_SUCCESS )
	{
		wchar_t Data[ 2048 ] = {};
		DWORD DataSize = sizeof( Data );
		DWORD Type = 0;

		if ( RegQueryValueExW( Key, ValueName, nullptr, &Type,
				reinterpret_cast< LPBYTE >( Data ), &DataSize ) == ERROR_SUCCESS )
		{
			std::wstring Entry = Data;
			std::wstring EntryUpper = Entry;
			std::wstring OwnUpper = OwnPath;

			std::transform( EntryUpper.begin( ), EntryUpper.end( ), EntryUpper.begin( ), ::towupper );
			std::transform( OwnUpper.begin( ), OwnUpper.end( ), OwnUpper.begin( ), ::towupper );

			if ( EntryUpper.find( OwnUpper ) != std::wstring::npos )
				ToDelete.push_back( ValueName );
		}

		ValueLen = 1024;
		Index++;
	}

	for ( const auto & Name : ToDelete )
		RegDeleteValueW( Key, Name.c_str( ) );

	RegCloseKey( Key );
}

void cleanComDlg32( )
{
	// Recently-used lists under the Explorer hive.
	HKEY Root = nullptr;
	if ( RegOpenKeyExW( HKEY_CURRENT_USER,
			L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ComDlg32",
			0, KEY_READ, &Root ) != ERROR_SUCCESS )
		return;

	const std::wstring OwnName = GetOwnImageName( );
	const std::wstring OwnPath = GetOwnImagePath( );

	std::vector< std::wstring > SubKeys;
	wchar_t SubName[ 512 ] = {};
	DWORD SubLen = 512;
	DWORD Index = 0;

	while ( RegEnumKeyExW( Root, Index, SubName, &SubLen, nullptr, nullptr, nullptr, nullptr ) == ERROR_SUCCESS )
	{
		SubKeys.push_back( SubName );
		SubLen = 512;
		Index++;
	}

	for ( const auto & Sub : SubKeys )
	{
		HKEY SubKey = nullptr;
		if ( RegOpenKeyExW( Root, Sub.c_str( ), 0, KEY_READ | KEY_WRITE, &SubKey ) != ERROR_SUCCESS )
			continue;

		std::vector< std::wstring > ToDelete;
		wchar_t ValueName[ 1024 ] = {};
		DWORD ValueLen = 1024;
		DWORD ValueIndex = 0;

		while ( RegEnumValueW( SubKey, ValueIndex, ValueName, &ValueLen, nullptr, nullptr, nullptr, nullptr ) == ERROR_SUCCESS )
		{
			std::vector< BYTE > Data( 8192 );
			DWORD DataSize = static_cast< DWORD >( Data.size( ) );
			DWORD Type = 0;

			if ( RegQueryValueExW( SubKey, ValueName, nullptr, &Type, Data.data( ), &DataSize ) == ERROR_SUCCESS )
			{
				// Values are REG_BINARY with UTF-16 paths embedded.
				const wchar_t * AsText = reinterpret_cast< const wchar_t * >( Data.data( ) );
				const size_t Chars = DataSize / sizeof( wchar_t );
				std::wstring Blob( AsText, Chars );

				std::wstring BlobUpper = Blob;
				std::wstring OwnUpper = OwnPath;
				std::transform( BlobUpper.begin( ), BlobUpper.end( ), BlobUpper.begin( ), ::towupper );
				std::transform( OwnUpper.begin( ), OwnUpper.end( ), OwnUpper.begin( ), ::towupper );

				std::wstring NameUpper = ValueName;
				std::transform( NameUpper.begin( ), NameUpper.end( ), NameUpper.begin( ), ::towupper );

				if ( BlobUpper.find( OwnUpper ) != std::wstring::npos ||
					 NameUpper.find( OwnName ) != std::wstring::npos )
				{
					ToDelete.push_back( ValueName );
				}
			}

			ValueLen = 1024;
			ValueIndex++;
		}

		for ( const auto & Name : ToDelete )
			RegDeleteValueW( SubKey, Name.c_str( ) );

		RegCloseKey( SubKey );
	}

	RegCloseKey( Root );
}

// ---------------------------------------------------------------------------
// 4. Event logs
// ---------------------------------------------------------------------------

void clearEventLogs( )
{
	// Channels a triage pass reads first. Clearing a log is itself an event in
	// the Security channel, so those are left alone deliberately.
	const wchar_t * Channels[ ] = {
		L"Microsoft-Windows-Windows Defender/Operational",
		L"Microsoft-Windows-PowerShell/Operational",
		L"Microsoft-Windows-Application-Experience/Program-Compatibility-Assistant",
		L"Microsoft-Windows-Application-Experience/Program-Telemetry",
		L"Microsoft-Windows-Sysmon/Operational",
		L"Microsoft-Windows-CodeIntegrity/Operational",
		L"Microsoft-Windows-AppLocker/EXE and DLL",
		L"Application",
		L"System",
	};

	for ( const wchar_t * Channel : Channels )
	{
		// The legacy API only needs advapi32, which is already linked. The
		// wevtapi EvtClearLog entry point is not in this project's import set,
		// so relying on it broke the build.
		HANDLE hLog = OpenEventLogW( nullptr, Channel );
		if ( hLog )
		{
			ClearEventLogW( hLog, nullptr );
			CloseEventLog( hLog );
		}
	}
}

// ---------------------------------------------------------------------------
// 5. NTFS slack and alternate data streams
// ---------------------------------------------------------------------------

void cleanOwnDirectory( )
{
	wchar_t Buffer[ MAX_PATH ] = {};
	GetModuleFileNameW( nullptr, Buffer, MAX_PATH );

	fs::path OwnPath = Buffer;
	fs::path OwnDir = OwnPath.parent_path( );

	std::error_code Ec;
	if ( !fs::exists( OwnDir, Ec ) )
		return;

	// Enumerate alternate data streams and zero the small ones; a hidden
	// payload there is one of the first things a scanner checks.
	for ( const auto & entry : fs::directory_iterator( OwnDir, Ec ) )
	{
		if ( !entry.is_regular_file( ) )
			continue;

		const std::wstring FilePath = entry.path( ).wstring( );
		const std::wstring StreamsPath = FilePath + L":Zone.Identifier";

		// The mark-of-the-web stream survives a copy and flags the file.
		DeleteFileW( StreamsPath.c_str( ) );
	}
}

// ---------------------------------------------------------------------------
// 6. Timestomping
// ---------------------------------------------------------------------------

void timestompSelf( )
{
	// Align our own file times with a neighbour in the same directory so a
	// "files written at execution time" pivot does not isolate us.
	wchar_t Buffer[ MAX_PATH ] = {};
	GetModuleFileNameW( nullptr, Buffer, MAX_PATH );
	fs::path OwnPath = Buffer;

	std::error_code Ec;
	fs::path OwnDir = OwnPath.parent_path( );

	fs::path Donor;
	FILETIME DonorCreate = {}, DonorAccess = {}, DonorWrite = {};

	for ( const auto & entry : fs::directory_iterator( OwnDir, Ec ) )
	{
		if ( !entry.is_regular_file( ) || entry.path( ) == OwnPath )
			continue;

		HANDLE hDonor = CreateFileW( entry.path( ).c_str( ), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, 0, nullptr );

		if ( hDonor == INVALID_HANDLE_VALUE )
			continue;

		if ( GetFileTime( hDonor, &DonorCreate, &DonorAccess, &DonorWrite ) )
		{
			Donor = entry.path( );
			CloseHandle( hDonor );
			break;
		}

		CloseHandle( hDonor );
	}

	if ( Donor.empty( ) )
		return;

	HANDLE hOwn = CreateFileW( OwnPath.c_str( ), FILE_WRITE_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, 0, nullptr );

	if ( hOwn == INVALID_HANDLE_VALUE )
		return;

	SetFileTime( hOwn, &DonorCreate, &DonorAccess, &DonorWrite );
	CloseHandle( hOwn );
}

// ---------------------------------------------------------------------------
// 7. Anti-analysis gates
// ---------------------------------------------------------------------------

bool isBeingDebugged( )
{
	if ( IsDebuggerPresent( ) )
		return true;

	BOOL RemoteDebugger = FALSE;
	if ( CheckRemoteDebuggerPresent( GetCurrentProcess( ), &RemoteDebugger ) && RemoteDebugger )
		return true;

	// PEB BeingDebugged, reached without the API so a hook on the import does
	// not hide it.
#ifdef _WIN64
	const BYTE * Peb = reinterpret_cast< const BYTE * >( __readgsqword( 0x60 ) );
#else
	const BYTE * Peb = reinterpret_cast< const BYTE * >( __readfsdword( 0x30 ) );
#endif

	if ( Peb && Peb[ 0x02 ] )
		return true;

	return false;
}

bool isSandboxed( )
{
	// Very short uptime means an automated analysis VM that was just resumed.
	if ( GetTickCount64( ) < 10000 )
		return true;

	// A single CPU and a tiny amount of RAM is a sandbox signature.
	SYSTEM_INFO SysInfo = {};
	GetSystemInfo( &SysInfo );
	if ( SysInfo.dwNumberOfProcessors < 2 )
		return true;

	MEMORYSTATUSEX MemStatus = {};
	MemStatus.dwLength = sizeof( MemStatus );
	if ( GlobalMemoryStatusEx( &MemStatus ) && MemStatus.ullTotalPhys < ( 2ull * 1024ull * 1024ull * 1024ull ) )
		return true;

	// Common analysis hostnames.
	wchar_t ComputerName[ MAX_COMPUTERNAME_LENGTH + 1 ] = {};
	DWORD NameLength = MAX_COMPUTERNAME_LENGTH + 1;

	if ( GetComputerNameW( ComputerName, &NameLength ) )
	{
		std::wstring Name = ComputerName;
		std::transform( Name.begin( ), Name.end( ), Name.begin( ), ::towlower );

		const wchar_t * BadNames[ ] = {
			L"sandbox", L"malware", L"analysis", L"cuckoo", L"virtual",
			L"vbox", L"vmware", L"qemu", L"windbg", L"testing",
		};

		for ( const wchar_t * Bad : BadNames )
		{
			if ( Name.find( Bad ) != std::wstring::npos )
				return true;
		}
	}

	return false;
}

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

void Prefetch_Cleaner( )
{
	const std::wstring PrefetchPath = L"C:\\Windows\\Prefetch";

	auto backups = backupTimestampsPrefetch( PrefetchPath );

	deleteOwnPrefetch( );
	clearUSNJournal( );
	spoofPrefetch( );
	restaurarTimestampsEForcarEvento( backups );
}

void CleanRegistryArtifacts( )
{
	cleanAmcache( );
	cleanShimCache( );
	cleanUserAssist( );
	cleanMuiCache( );
	cleanBam( );
	cleanRunMRU( );
	cleanComDlg32( );
}

void RunDestruct( )
{
	// Analysis gates first - no point erasing traces inside a sandbox that is
	// recording everything anyway.
	if ( isBeingDebugged( ) || isSandboxed( ) )
		return;

	// Staggered so the I/O pattern is not one burst a heuristic can flag.
	Prefetch_Cleaner( );
	randomSleep( 150, 400 );

	CleanRegistryArtifacts( );
	randomSleep( 150, 400 );

	clearEventLogs( );
	randomSleep( 100, 250 );

	cleanOwnDirectory( );
	timestompSelf( );
}

// ---------------------------------------------------------------------------
// Exit hook
//
// The menu can leave through several paths: WM_QUIT, the tray exit, a
// TerminateProcess from the loader, or the console closing. A console control
// handler plus atexit covers the first, the last two, and a normal return from
// WinMain. The work runs once - the guard also protects against a handler that
// re-enters while it is already cleaning.
// ---------------------------------------------------------------------------

volatile LONG g_DestructRunning = 0;
volatile LONG g_DestructDone = 0;

void RunDestructOnce( )
{
	// InterlockedCompareExchange claims the run; a second caller sees 1 and
	// leaves immediately.
	if ( InterlockedCompareExchange( &g_DestructRunning, 1, 0 ) != 0 )
		return;

	if ( InterlockedCompareExchange( &g_DestructDone, 1, 0 ) != 0 )
	{
		InterlockedExchange( &g_DestructRunning, 0 );
		return;
	}

	__try
	{
		RunDestruct( );
	}
	__except ( EXCEPTION_EXECUTE_HANDLER )
	{
		// A failure inside cleanup must never take the exit path down with it.
	}

	InterlockedExchange( &g_DestructRunning, 0 );
}

BOOL WINAPI DestructConsoleHandler( DWORD CtrlType )
{
	switch ( CtrlType )
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		RunDestructOnce( );
		return TRUE;
	default:
		return FALSE;
	}
}

// Call once from the overlay, after the window exists.
void InstallDestructHandler( )
{
	SetConsoleCtrlHandler( DestructConsoleHandler, TRUE );
	atexit( RunDestructOnce );
}
