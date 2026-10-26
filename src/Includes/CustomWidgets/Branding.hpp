#pragma once
#include <Includes/Includes.hpp>

// ---------------------------------------------------------------------------
// Branding - the "Art Bypass" mark drawn into the left rail of the menu.
//
// Drawn directly into the window draw list rather than as a widget so it does
// not participate in layout: the rail is a fixed 85 px column and the page
// content starts at x = 140.
// ---------------------------------------------------------------------------

namespace Custom
{
	namespace Branding
	{
		inline void Render( bool Logged )
		{
			if ( !Logged )
				return;

			ImDrawList * DrawList = ImGui::GetWindowDrawList( );
			if ( !DrawList )
				return;

			const ImVec2 WindowPos = ImGui::GetWindowPos( );

			// Anchored to the bottom of the left rail, below the tab column.
			const float RailWidth = 85.f;
			const float BaseY = WindowPos.y + g_MenuInfo.MenuSize.y - 46.f;

			const char * Line1 = xorstr( "Art" );
			const char * Line2 = xorstr( "Bypass" );

			ImFont * Font = g_Variables.m_FontSecundary ? g_Variables.m_FontSecundary
			                                            : ImGui::GetFont( );
			const float FontSize = Font->FontSize;

			const ImVec2 Size1 = Font->CalcTextSizeA( FontSize, FLT_MAX, 0.f, Line1 );
			const ImVec2 Size2 = Font->CalcTextSizeA( FontSize, FLT_MAX, 0.f, Line2 );

			const float X1 = WindowPos.x + ( RailWidth * 0.5f ) - ( Size1.x * 0.5f );
			const float X2 = WindowPos.x + ( RailWidth * 0.5f ) - ( Size2.x * 0.5f );

			// Soft accent rule under the mark, kept inside the rail.
			const float RuleY = BaseY + Size1.y + Size2.y + 8.f;
			DrawList->AddRectFilled(
				ImVec2( WindowPos.x + 18.f, RuleY ),
				ImVec2( WindowPos.x + RailWidth - 18.f, RuleY + 2.f ),
				ImColor( 136, 100, 255, 180 ), 1.f );

			// Dark backing so the mark reads over any background.
			DrawList->AddRectFilled(
				ImVec2( WindowPos.x + 6.f, BaseY - 6.f ),
				ImVec2( WindowPos.x + RailWidth - 6.f, RuleY + 8.f ),
				ImColor( 8, 8, 10, 190 ), 6.f );

			DrawList->AddText( Font, FontSize, ImVec2( X1 + 1.f, BaseY + 1.f ),
			                   ImColor( 0, 0, 0, 220 ), Line1 );
			DrawList->AddText( Font, FontSize, ImVec2( X1, BaseY ),
			                   ImColor( 170, 170, 170, 255 ), Line1 );

			DrawList->AddText( Font, FontSize, ImVec2( X2 + 1.f, BaseY + Size1.y + 1.f ),
			                   ImColor( 0, 0, 0, 220 ), Line2 );
			DrawList->AddText( Font, FontSize, ImVec2( X2, BaseY + Size1.y ),
			                   ImColor( 136, 100, 255, 255 ), Line2 );
		}
	}
}
