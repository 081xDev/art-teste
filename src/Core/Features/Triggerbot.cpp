#include "Triggerbot.hpp"

// ---------------------------------------------------------------------------
// Triggerbot - fires the attack input when a target is under the crosshair.
//
// Defects fixed here:
//
//  * Delay was ignored. The condition was `if (!ShotInFlight || SinceLast >=
//    Delay)`, and ShotInFlight was cleared the instant the target left the
//    FOV. Since the FOV test is per-frame, an aim that flickered across the
//    boundary re-entered with the flag false and fired immediately. The gate
//    is now purely on elapsed time.
//
//  * Delay was applied by sleeping INSIDE the shot path, before the click, so
//    the real interval was Delay + loop time and the whole loop stalled. The
//    click now goes out immediately and the interval is measured afterwards.
//
//  * The click is one SendInput pair, so the button is never held. The old
//    code still tracked an in-flight shot and fired an extra LEFTOP on the
//    deactivate and target-lost paths, delivering spurious button-ups to the
//    game with no matching down.
//
//  * `continue` on "no target" skipped the trailing sleep, spinning the thread
//    at 100% CPU whenever nothing was under the crosshair.
// ---------------------------------------------------------------------------

namespace
{
	// Sends exactly one down/up pair in a single call, so the button can never
	// be left held and no release has to be tracked.
	void SendClick( )
	{
		INPUT Inputs[ 2 ] = {};

		Inputs[ 0 ].type = INPUT_MOUSE;
		Inputs[ 0 ].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

		Inputs[ 1 ].type = INPUT_MOUSE;
		Inputs[ 1 ].mi.dwFlags = MOUSEEVENTF_LEFTUP;

		SendInput( 2, Inputs, sizeof( INPUT ) );
	}
}

void Core::Features::cTriggerbot::Shoot( int delay )
{
	// The click goes out immediately; `delay` is enforced by the caller as a
	// minimum interval between shots, not as a blocking sleep here.
	(void)delay;
	SendClick( );
}

void Core::Features::cTriggerbot::Start( )
{
	// Back-dated so the first shot is not held back by the initial interval.
	auto LastShot = std::chrono::steady_clock::now( ) - std::chrono::seconds( 1 );

	while ( true )
	{
		const bool Active = g_Config.TriggerBot->Enabled
			&& g_Config.TriggerBot->KeyBind != 0
			&& ( GetAsyncKeyState( g_Config.TriggerBot->KeyBind ) & 0x8000 ) != 0
			&& GetForegroundWindow( ) != g_Variables.g_hCheatWindow;

		if ( !Active )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			continue;
		}

		bool TargetUnderCrosshair = false;

		if ( !g_Config.TriggerBot->SmartTrigger )
		{
			CPed * Ped = Core::SDK::Game::GetClosestPed(
				g_Config.TriggerBot->MaxDistance,
				g_Config.TriggerBot->IgnoreNPCs,
				g_Config.TriggerBot->OnlyVisible );

			if ( Ped )
			{
				const D3DXVECTOR3 HeadPos = Ped->GetBonePosDefault( 0 );
				const D3DXVECTOR2 ScreenHeadPos = Core::SDK::Game::WorldToScreen( HeadPos );

				if ( Core::SDK::Game::IsOnScreen( ScreenHeadPos ) )
				{
					const int Fov = static_cast< int >( std::hypot(
						ScreenHeadPos.x - g_Variables.g_vGameWindowCenter.x,
						ScreenHeadPos.y - g_Variables.g_vGameWindowCenter.y ) );

					TargetUnderCrosshair = ( Fov < g_Config.TriggerBot->FOV );
				}
			}
		}
		else
		{
			// Smart mode reads the game's own resolved aim target.
			const uintptr_t AimResult = g_Offsets.m_AimCPedPatternResult;

			if ( AimResult )
			{
				const int Relative = Mem.Read<int>( AimResult + 3 );
				if ( Relative != 0 )
				{
					CPed * Ped = Mem.Read<CPed *>( AimResult + Relative + 7 );
					TargetUnderCrosshair = ( Ped != nullptr );
				}
			}
		}

		if ( TargetUnderCrosshair )
		{
			const auto Now = std::chrono::steady_clock::now( );
			const auto SinceLast = std::chrono::duration_cast<std::chrono::milliseconds>( Now - LastShot ).count( );

			if ( SinceLast >= g_Config.TriggerBot->Delay )
			{
				Shoot( g_Config.TriggerBot->Delay );
				LastShot = std::chrono::steady_clock::now( );
			}
		}

		std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
	}
}
