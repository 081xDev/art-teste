#include "Memory.hpp"
#include <filesystem>
#include <winternl.h>
#include <Auth/lazyimporter.hpp>
#include <Globals.hpp>

namespace Core {

	uintptr_t MemoryClass::PatternScan(std::vector<uint8_t> Pattern, int InstructionLength)
	{
		uintptr_t Address = FindSignature(Pattern);

		if (InstructionLength != 0)
		{
			Address = ResolveRelativeAddress(Address, InstructionLength);
		}

		return Address;
	}

	uintptr_t MemoryClass::FindSignature(std::vector<uint8_t> Signature, uintptr_t ModuleBase, uintptr_t ModuleBaseSize)
	{
		const size_t blockSize = (4096 * 4);
		std::unique_ptr<uint8_t[]> data = std::make_unique<uint8_t[]>(blockSize);

		DWORD oldProtect;
		size_t signatureSize = Signature.size();

		uintptr_t ModulBase;
		uintptr_t ModulBaseSize;

		if (ModuleBase != 0 && ModuleBaseSize != 0) {
			ModulBase = ModuleBase;
			ModulBaseSize = ModuleBaseSize;
		}
		else {
			ModulBase = ModBase;
			ModulBaseSize = ModBaseSize;
		}

		for (uintptr_t address = ModulBase; address < ModulBase + ModulBaseSize; address += blockSize)
		{
			if (!VirtualProtectEx(ProcHandle, (LPVOID)address, blockSize, PAGE_EXECUTE_READWRITE, &oldProtect)) { continue; }

			SIZE_T bytesRead;
			if (!ReadProcessMemory(ProcHandle, (void*)address, data.get(), blockSize, &bytesRead)) {
				VirtualProtectEx(ProcHandle, (LPVOID)address, blockSize, oldProtect, NULL);
				continue;
			}

			VirtualProtectEx(ProcHandle, (LPVOID)address, blockSize, oldProtect, NULL);

			for (uintptr_t i = 0; i < bytesRead; i++)
			{
				// A match started near the end of the block would read past the
				// 16 KB buffer. Stop where a full signature can still fit.
				if (i + signatureSize > bytesRead)
					break;

				for (uintptr_t j = 0; j < signatureSize; j++)
				{
					if (Signature[j] == 0x00)
						continue;

					if (data[i + j] != Signature[j])
						break;

					if (j == signatureSize - 1)
						return (address + i);
				}
			}
		}

		return 0x0;
	}

	uintptr_t MemoryClass::FindSignatureBypass(std::vector<uint8_t> Signature, uintptr_t ModuleBase, uintptr_t ModuleBaseSize)
	{
		const size_t blockSize = (4096 * 4);
		std::unique_ptr<uint8_t[]> data = std::make_unique<uint8_t[]>(blockSize);

		DWORD oldProtect;
		size_t signatureSize = Signature.size();

		uintptr_t ModulBase;
		uintptr_t ModulBaseSize;

		if (ModuleBase != 0 && ModuleBaseSize != 0) {
			ModulBase = ModuleBase;
			ModulBaseSize = ModuleBaseSize;
		}
		else {
			ModulBase = ModBase;
			ModulBaseSize = ModBaseSize;
		}

		for (uintptr_t address = ModulBase; address < ModulBase + ModulBaseSize; address += blockSize)
		{
			//if (!VirtualProtectEx(ProcHandle, (LPVOID)address, blockSize, PAGE_EXECUTE_READWRITE, &oldProtect)) { continue; }

			SIZE_T bytesRead;
			if (!ReadProcessMemory(ProcHandle, (void*)address, data.get(), blockSize, &bytesRead)) {
				//VirtualProtectEx(ProcHandle, (LPVOID)address, blockSize, oldProtect, NULL);
				continue;
			}

			//VirtualProtectEx(ProcHandle, (LPVOID)address, blockSize, oldProtect, NULL);

			for (uintptr_t i = 0; i < bytesRead; i++)
			{
				// A match started near the end of the block would read past the
				// 16 KB buffer. Stop where a full signature can still fit.
				if (i + signatureSize > bytesRead)
					break;

				for (uintptr_t j = 0; j < signatureSize; j++)
				{
					if (Signature[j] == 0x00)
						continue;

					if (data[i + j] != Signature[j])
						break;

					if (j == signatureSize - 1)
						return (address + i);
				}
			}
		}

		return 0x0;
	}

	uintptr_t MemoryClass::FindSignatureStr(std::string Pattern, uintptr_t ModuleBase, uintptr_t ModuleBaseSize) {
		std::vector<uint8_t> signature = Pattern2Vector(Pattern);
		if (ModuleBase != 0 && ModuleBaseSize != 0) {
			return FindSignature(signature, ModuleBase, ModuleBaseSize);
		}
		else {
			return FindSignature(signature); // Find Sig in module from open proc.
		}
	}

	std::vector<uint8_t> MemoryClass::ReadBytes(uintptr_t Addr, size_t Size) {
		std::vector<uint8_t> bytes(Size);
		size_t bytesRead = 0;
		ZwReadVirtualMemory(ProcHandle, (LPCVOID)Addr, bytes.data(), Size, &bytesRead);

		// A short read used to leave the vector smaller than requested, and the
		// caller then "restored" the original bytes with a truncated buffer -
		// writing fewer bytes back than were overwritten and corrupting the
		// game's code. Keep the full size so the caller can detect the failure.
		if (bytesRead != Size)
			bytes.resize(bytesRead);

		return bytes;
	}

	BOOL MemoryClass::WriteBytes(uintptr_t Addr, std::vector<uint8_t> Bytes) {
		NTSTATUS status = ZwWriteVirtualMemory(ProcHandle, (LPVOID)Addr, Bytes.data(), Bytes.size(), NULL);
		return NT_SUCCESS(status);
	}

	BOOL MemoryClass::PatchFunc(uintptr_t Addr, int NopCount)
	{
		if (!NopCount) return false;

		std::vector<uint8_t> PatchTable = { };
		for (int i = 0; i < NopCount; i++)
		{
			PatchTable.push_back({ 0x90 });
		}

		return WriteBytes(Addr, PatchTable);
	}


	BOOL MemoryClass::GetMaxPrivileges(HANDLE hProc) {
		HANDLE h_Token = nullptr;

		if (!OpenProcessToken(hProc, TOKEN_READ | TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &h_Token))
			return false;

		// Size the buffer from the API before allocating. The old code used a
		// fixed 100-element array and then indexed it with the count the API
		// returned, so a token with more than 100 privileges wrote past the
		// allocation. It also used the wrong stride - TOKEN_PRIVILEGES ends in
		// a one-element array, so indexing the struct itself steps by
		// sizeof(TOKEN_PRIVILEGES) rather than by LUID_AND_ATTRIBUTES.
		DWORD dw_TokenLength = 0;
		GetTokenInformation(h_Token, TokenPrivileges, nullptr, 0, &dw_TokenLength);

		if (dw_TokenLength == 0)
		{
			CloseHandle(h_Token);
			return false;
		}

		std::vector<uint8_t> Buffer(dw_TokenLength);

		if (!GetTokenInformation(h_Token, TokenPrivileges, Buffer.data(), dw_TokenLength, &dw_TokenLength))
		{
			CloseHandle(h_Token);
			return false;
		}

		TOKEN_PRIVILEGES * Privileges = reinterpret_cast<TOKEN_PRIVILEGES *>(Buffer.data());

		if (Privileges->PrivilegeCount == 0)
		{
			CloseHandle(h_Token);
			return false;
		}

		// Guard the count against the buffer we actually hold, so a bogus
		// count from a hooked or hostile API cannot drive the loop out of
		// bounds either.
		const size_t MaxCount =
			(dw_TokenLength - offsetof(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES);

		if (Privileges->PrivilegeCount > MaxCount)
		{
			CloseHandle(h_Token);
			return false;
		}

		for (DWORD i = 0; i < Privileges->PrivilegeCount; i++)
			Privileges->Privileges[i].Attributes = SE_PRIVILEGE_ENABLED;

		const BOOL Adjusted = AdjustTokenPrivileges(h_Token, FALSE, Privileges, dw_TokenLength, nullptr, nullptr);

		// AdjustTokenPrivileges reports success even when it applied nothing.
		const BOOL AllApplied = Adjusted && (GetLastError() == ERROR_SUCCESS);

		CloseHandle(h_Token);
		return AllApplied;
	}

	std::string MemoryClass::ReadString(uintptr_t Addr) {
		const int bufferSize = 256;
		char buffer[bufferSize];
		int bytesRead = 0;
		bool success = true;

		while (bytesRead < bufferSize) {
			char character;
			ZwReadVirtualMemory(ProcHandle, (LPVOID)(Addr + bytesRead), &character, sizeof(char), NULL);
			buffer[bytesRead] = character;

			if (character == '\0') { break; }

			bytesRead++;
		}

		if (bytesRead == bufferSize) { success = false; }

		if (!success) { return ""; }

		return std::string(buffer);
	}

	DWORD MemoryClass::GetPidByName(const char* ProcName) {
		HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnapshot == INVALID_HANDLE_VALUE) {
			return 0;
		}

		PROCESSENTRY32 pe32;
		pe32.dwSize = sizeof(PROCESSENTRY32);

		if (!Process32First(hSnapshot, &pe32)) {
			CloseHandle(hSnapshot);
			return 0;
		}

		DWORD pid = 0;
		do {
			if (_stricmp(pe32.szExeFile, ProcName) == 0) {
				pid = pe32.th32ProcessID;
				break;
			}
		} while (Process32Next(hSnapshot, &pe32));

		CloseHandle(hSnapshot);
		return pid;
	}

	std::string MemoryClass::GetNameByPid(DWORD Pid) {
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, Pid);
		if (!hProcess) { return ""; }

		char processName[MAX_PATH];
		if (!GetModuleFileNameEx(hProcess, NULL, processName, MAX_PATH)) { return ""; }

		CloseHandle(hProcess);

		std::string fullPath(processName);
		size_t pos = fullPath.find_last_of(xorstr("\\/"));
		if (pos != std::string::npos) {
			return fullPath.substr(pos + 1);
		}
		else {
			return fullPath;
		}
	}

	uintptr_t MemoryClass::GetModuleBaseAddr(DWORD ProcId, const char* ModuleName, uintptr_t* ModSize) {
		HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, ProcId);

		if (hSnapShot == INVALID_HANDLE_VALUE) { return 0; }

		MODULEENTRY32 ModuleEntry;
		ModuleEntry.dwSize = sizeof(MODULEENTRY32);
		if (Module32First(hSnapShot, &ModuleEntry)) {
			do {
				if (strcmp(ModuleEntry.szModule, ModuleName) == 0) {
					break;
				}
			} while (Module32Next(hSnapShot, &ModuleEntry));
		}
		CloseHandle(hSnapShot);

		*ModSize = (uintptr_t)ModuleEntry.modBaseSize;
		return (uintptr_t)ModuleEntry.modBaseAddr;
	}


	/*uint64_t MemoryClass::GetModuleBaseByName(DWORD Pid, std::wstring ModuleName)
	{
		HANDLE hSnapshot = SafeCall(CreateToolhelp32Snapshot)(TH32CS_SNAPMODULE, Pid);
		if (!hSnapshot || hSnapshot == INVALID_HANDLE_VALUE || hSnapshot == ((HANDLE)(LONG_PTR)ERROR_BAD_LENGTH))
		{
			return 0;
		}

		uint64_t ModuleBase;
		MODULEENTRY32 ModuleEntry;
		ModuleEntry.dwSize = sizeof(ModuleEntry);
		if (SafeCall(Module32First)(hSnapshot, &ModuleEntry))
		{
			while (_wcsicmp(ModuleEntry.szModule, ModuleName.c_str()))
			{
				if (!SafeCall(Module32Next)(hSnapshot, &ModuleEntry))
				{
					SafeCall(CloseHandle)(hSnapshot);
					return 0;
				}
			}

			ModuleBase = (uint64_t)ModuleEntry.modBaseAddr;
		}
		else
		{
			SafeCall(CloseHandle)(hSnapshot);
			return 0;
		}

		SafeCall(CloseHandle)(hSnapshot);
		return ModuleBase;
	}

	uint64_t MemoryClass::GetModuleBaseSizeByName(DWORD Pid, std::wstring ModuleName)
	{
		HANDLE hSnapshot = SafeCall(CreateToolhelp32Snapshot)(TH32CS_SNAPMODULE, Pid);
		if (!hSnapshot || hSnapshot == INVALID_HANDLE_VALUE || hSnapshot == ((HANDLE)(LONG_PTR)ERROR_BAD_LENGTH))
		{
			return 0;
		}

		uint64_t ModuleBase;
		MODULEENTRY32 ModuleEntry;
		ModuleEntry.dwSize = sizeof(ModuleEntry);
		if (SafeCall(Module32First)(hSnapshot, &ModuleEntry))
		{
			while (_wcsicmp(ModuleEntry.szModule, ModuleName.c_str()))
			{
				if (!SafeCall(Module32Next)(hSnapshot, &ModuleEntry))
				{
					SafeCall(CloseHandle)(hSnapshot);
					return 0;
				}
			}

			ModuleBase = (uint64_t)ModuleEntry.modBaseSize;
		}
		else
		{
			SafeCall(CloseHandle)(hSnapshot);
			return 0;
		}

		SafeCall(CloseHandle)(hSnapshot);

		return ModuleBase;
	}*/

	BOOL MemoryClass::OpenProcByPid() {
		if (g_Variables.ProcIdFiveM == 0) { return false; }

		ProcHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, g_Variables.ProcIdFiveM);
		std::cout << "Process Handle: " << ProcHandle << std::endl;
		if (!ProcHandle) { return false; };

		/*Mem.ModBase = Mem.GetModuleBaseByName(g_Variables.ProcIdFiveM, Mem.ProcName);
		Mem.ModBaseSize = Mem.GetModuleBaseSizeByName(g_Variables.ProcIdFiveM, Mem.ProcName);*/
		std::string procNameStr(Mem.ProcName.begin(), Mem.ProcName.end()); 
		Mem.ModBase = Mem.GetModuleBaseAddr(g_Variables.ProcIdFiveM, procNameStr.c_str(), &ModBaseSize);


		return true;
	}

}