#pragma once
#include <Includes/Includes.hpp>
#include <Core/Offsets.hpp>
#include <Core/SDK/SDK.hpp>

namespace Core
{
	namespace Features
	{
		class cSilentAim
		{
		private:
			// Tracks whether the trampoline currently owns StartAddy. Only
			// touched from the worker thread, so a plain bool is enough.
			bool SilentAimInitialized = false;

			uintptr_t StartAddy = 0;
			uintptr_t SilentAimHook = 0;

			std::vector<std::uint8_t> OriginalFuncTable;

			// Payload layout, offsets are load-bearing - see SilentAim.cpp for
			// the matching memcpy() calls.
			//   0   : C7 45 07        mov [rbp+07], imm32   (X)
			//   7   : C7 45 0B        mov [rbp+0B], imm32   (Y)
			//   14  : C7 45 0F        mov [rbp+0F], imm32   (Z)
			//   21  : FF 25 00 00 00 00  jmp qword ptr [rip+0]
			//   27  : <8 byte return address>
			// Total 35 bytes. The previous vector was 34 bytes long while the
			// code wrote 8 bytes at offset 27, running one byte past the end of
			// the buffer on every write.
			std::vector<std::uint8_t> SilentAimShell =
			{
				0xC7, 0x45, 0x07,
				0x00, 0x00, 0x00, 0x00,

				0xC7, 0x45, 0x0B,
				0x00, 0x00, 0x00, 0x00,

				0xC7, 0x45, 0x0F,
				0x00, 0x00, 0x00, 0x00,

				0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
			};

			void WriteShell( const D3DXVECTOR3 & Target );
		public:
			void InitializeSilentAim( );
			void RestoreSilent( );
			void HookSilent( );
		};

		inline cSilentAim g_SilentAim;

	}
}
