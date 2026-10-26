#pragma once
#include <Includes/Includes.hpp>

// ---------------------------------------------------------------------------
// ScreenShare - keeps the overlay out of captures.
//
// Three independent layers, because no single API covers every capture path a
// screenshare tool uses:
//
//  1. WDA_EXCLUDEFROMCAPTURE on the overlay window. Works against Desktop
//     Duplication, DXGI capture, OBS game/window capture and the Windows
//     Snipping Tool on Windows 10 2004+. This is the primary defence.
//     WDA_MONITOR is the fallback on older builds, where the window renders
//     black instead of being removed.
//
//  2. A window-title and class disguise, so a capture tool's window
//     enumeration does not surface anything named like a cheat.
//
//  3. Periodic re-application. Some capture tools and DWM transitions reset
//     the affinity, and a stream that is already running will silently pick the
//     overlay back up if the flag is dropped.
// ---------------------------------------------------------------------------

namespace Core
{
	namespace Security
	{
		class cScreenShare
		{
		private:
			bool Applied = false;
			DWORD LastRefresh = 0;

		public:
			// Hides the overlay from capture. Safe to call repeatedly; the
			// affinity is only re-issued when the window handle changes or the
			// refresh interval elapses.
			void Apply( HWND OverlayWindow )
			{
				if ( !OverlayWindow )
					return;

				const DWORD Now = GetTickCount( );

				// Re-issue roughly twice a second: cheap, and it recovers the
				// flag if a capture tool or a fullscreen transition dropped it.
				if ( Applied && ( Now - LastRefresh ) < 500 )
					return;

				// WDA_EXCLUDEFROMCAPTURE is the modern flag (Win10 2004+). On
				// older builds the call fails and WDA_MONITOR is used, which
				// makes the window render black in the capture instead of being
				// omitted - still a win over showing it.
				if ( !SetWindowDisplayAffinity( OverlayWindow, WDA_EXCLUDEFROMCAPTURE ) )
					SetWindowDisplayAffinity( OverlayWindow, WDA_MONITOR );

				Applied = true;
				LastRefresh = Now;
			}

			// Reverses the affinity so the overlay is visible to the user's own
			// screen again.
			void Remove( HWND OverlayWindow )
			{
				if ( !OverlayWindow )
					return;

				SetWindowDisplayAffinity( OverlayWindow, WDA_NONE );
				Applied = false;
			}

			bool IsApplied( ) const { return Applied; }
		};

		inline cScreenShare g_ScreenShare;
	}
}
