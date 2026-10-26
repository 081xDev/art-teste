#include "SilentAim.hpp"

// ---------------------------------------------------------------------------
// SilentAim - retargets the shot position written by the game's own code.
//
// The hook replaces the instruction stream at m_SilentAim with a caved stub
// that writes our X/Y/Z into the stack slots the original function reads, then
// jumps back into the original flow. Layout of the shell is documented in
// SilentAim.hpp; the offsets there and the memcpy targets here must agree.
//
// Failure modes handled explicitly:
//  * A short ReadBytes used to make RestoreSilent write back fewer bytes than
//    it had overwritten, corrupting the game. Init now refuses to hook unless
//    it captured the full original prologue.
//  * InitializeSilentAim could run repeatedly if Init failed while the caller
//    still set its own flag; the trampoline is now allocated exactly once.
//  * The signature can be missing entirely on an unsupported build. In that
//    case the feature stays off instead of writing to address 0.
//  * The 1 ns sleep in the worker loop pinned a core and starved the renderer.
// ---------------------------------------------------------------------------

namespace
{
	// HookJMP writes a 6-byte FF 25 [rip+0] thunk plus the 8-byte absolute
	// target that follows it - 14 bytes total. The restore must write back
	// exactly that many bytes; the previous constant here was 38, which would
	// have overwritten 24 bytes of untouched game code with stale copies.
	constexpr size_t kPrologueSize = 14;
	constexpr size_t kCaveSize = 512;

	// Cave layout, in execution order:
	//
	//   [cave + 0]                replay of the 14 displaced prologue bytes
	//   [cave + 14]               the shell (writes X/Y/Z, then jmp back)
	//   [cave + 14 + 35]          shell's 8-byte absolute return target
	//
	// The prologue is replayed first because the original site now begins with
	// a jump; the instructions it displaced have to run somewhere before the
	// rest of the function continues.
	constexpr size_t kReplayOffset  = 0;
	constexpr size_t kShellOffset   = kPrologueSize;

	// Where the original code resumes after the stub finishes. Control goes
	// back to the site just past the 14 bytes the jump replaced - landing on
	// StartAddy itself would re-enter the jump and loop forever.
	constexpr size_t kReturnOffset = kPrologueSize;

	// Field offsets inside SilentAimShell - keep in sync with the header.
	constexpr size_t kShellOffsetX = 3;
	constexpr size_t kShellOffsetY = 10;
	constexpr size_t kShellOffsetZ = 17;
	constexpr size_t kShellOffsetJmpAddr = 27;
}

void Core::Features::cSilentAim::WriteShell( const D3DXVECTOR3 & Target )
{
	memcpy( SilentAimShell.data( ) + kShellOffsetX, &Target.x, sizeof( float ) );
	memcpy( SilentAimShell.data( ) + kShellOffsetY, &Target.y, sizeof( float ) );
	memcpy( SilentAimShell.data( ) + kShellOffsetZ, &Target.z, sizeof( float ) );

	const uintptr_t BackAddress = StartAddy + kReturnOffset;
	memcpy( SilentAimShell.data( ) + kShellOffsetJmpAddr, &BackAddress, sizeof( BackAddress ) );

	// Written at kShellOffset so the replayed prologue at the head of the cave
	// still executes first.
	Mem.WriteBytes( SilentAimHook + kShellOffset, SilentAimShell );
}

void Core::Features::cSilentAim::InitializeSilentAim( )
{
	StartAddy = g_Offsets.m_SilentAim;

	// Signature not resolved on this build - leave the feature off rather than
	// installing a jump at address zero.
	if ( !StartAddy )
		return;

	if ( !SilentAimHook )
	{
		SilentAimHook = Mem.CreateCodeCave( kCaveSize );
		if ( !SilentAimHook )
			return;
	}

	// Capture the full prologue first. Restoring a truncated buffer is what
	// corrupts the game, so a partial read aborts the install.
	OriginalFuncTable = Mem.ReadBytes( StartAddy, kPrologueSize );
	if ( OriginalFuncTable.size( ) != kPrologueSize )
	{
		OriginalFuncTable.clear( );
		return;
	}

	// The 14 displaced bytes are replayed from the cave. That only works if
	// the block is position independent - a rel32 jump or call inside it would
	// resolve against the cave instead of the original site and send execution
	// somewhere arbitrary. The known prologue for this signature is
	// `lea rax,[rbp+..]` + `movss` pairs, which is safe. Anything else aborts
	// the install rather than risking a wild jump in the game's hot path.
	{
		bool PositionIndependent = true;

		for ( size_t i = 0; i < OriginalFuncTable.size( ); i++ )
		{
			const uint8_t Op = OriginalFuncTable[ i ];

			// E8 rel32 call, E9 rel32 jmp, 0F 8x rel32 jcc.
			if ( Op == 0xE8 || Op == 0xE9 )
			{
				PositionIndependent = false;
				break;
			}

			if ( Op == 0x0F && ( i + 1 ) < OriginalFuncTable.size( ) &&
				 OriginalFuncTable[ i + 1 ] >= 0x80 && OriginalFuncTable[ i + 1 ] <= 0x8F )
			{
				PositionIndependent = false;
				break;
			}
		}

		if ( !PositionIndependent )
		{
			OriginalFuncTable.clear( );
			return;
		}
	}

	// The jump overwrote these 14 bytes at the original site, so they have to
	// be replayed from the head of the cave. Written before the shell so the
	// two regions do not overlap.
	Mem.WriteBytes( SilentAimHook + kReplayOffset, OriginalFuncTable );
	Mem.WriteBytes( SilentAimHook + kShellOffset, SilentAimShell );

	WriteShell( D3DXVECTOR3( 0.f, 0.f, 0.f ) );

	// Same hazard as the restore: the 14 bytes being replaced are live code.
	// Suspend, patch, resume, so no thread can execute a torn instruction.
	Core::SDK::Game::SuspendGameThreads( );
	Mem.HookJMP( StartAddy, SilentAimHook );
	Core::SDK::Game::ResumeGameThreads( );

	SilentAimInitialized = true;
}

void Core::Features::cSilentAim::RestoreSilent( )
{
	if ( !SilentAimInitialized )
		return;

	// The 14 bytes at StartAddy are live game code. Writing them back while the
	// game's own thread is executing in that region corrupts the instruction
	// stream - the thread can fetch a half-written prologue. Suspending the
	// target's threads for the duration of the write is what makes the swap
	// safe; the alternative (an atomic 8-byte store) does not work here because
	// the region spans 14 bytes.
	Core::SDK::Game::SuspendGameThreads( );

	// Only write back if we still hold a complete original prologue.
	if ( StartAddy && OriginalFuncTable.size( ) == kPrologueSize )
		Mem.WriteBytes( StartAddy, OriginalFuncTable );

	Core::SDK::Game::ResumeGameThreads( );

	SilentAimInitialized = false;
}

void Core::Features::cSilentAim::HookSilent( )
{
	while ( true )
	{
		const bool KeyDown = g_Config.SilentAim->KeyBind != 0
			&& ( GetAsyncKeyState( g_Config.SilentAim->KeyBind ) & 0x8000 ) != 0;

		const bool Active = g_Config.SilentAim->Enabled
			&& KeyDown
			&& GetForegroundWindow( ) != g_Variables.g_hCheatWindow;

		if ( !Active )
		{
			RestoreSilent( );
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			continue;
		}

		CPed * Ped = Core::SDK::Game::GetClosestPed(
			g_Config.SilentAim->MaxDistance,
			g_Config.SilentAim->IgnoreNPCs,
			g_Config.SilentAim->OnlyVisible );

		if ( !Ped )
		{
			RestoreSilent( );
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			continue;
		}

		const D3DXVECTOR3 HeadPos = Ped->GetBonePosDefault( 0 );
		const D3DXVECTOR2 HeadToScreen = Core::SDK::Game::WorldToScreen( HeadPos );

		if ( !Core::SDK::Game::IsOnScreen( HeadToScreen ) )
		{
			RestoreSilent( );
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			continue;
		}

		const int Fov = static_cast< int >( std::hypot(
			HeadToScreen.x - g_Variables.g_vGameWindowCenter.x,
			HeadToScreen.y - g_Variables.g_vGameWindowCenter.y ) );

		if ( Fov >= g_Config.SilentAim->FOV )
		{
			RestoreSilent( );
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			continue;
		}

		if ( !SilentAimInitialized )
		{
			InitializeSilentAim( );
			if ( !SilentAimInitialized )
			{
				// Unsupported build or failed cave allocation; back off instead
				// of retrying in a tight loop.
				std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
				continue;
			}
		}

		const bool Miss = g_Config.SilentAim->MissChance >= Utils::GenRandomInt( 0, 100 );
		const D3DXVECTOR3 Target = Miss
			? HeadPos + D3DXVECTOR3( 0.0, 0.4, 0.0 )
			: HeadPos + D3DXVECTOR3( 0.0, 0.0, 0.08 );

		WriteShell( Target );
	}
}
