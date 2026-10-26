#pragma once
#include <Includes/includes.hpp>
#include <windows.h>
#include <iostream>
#include <thread>
#include <Includes/CustomWidgets/Preview.hpp>

#include <Core/Core.hpp>

using namespace std;

namespace Visuals {

	static int iSubTabCount = 0;
	static float SubTabAlpha = 0.f;
	static int iSubTab = 0;

	// Side picker for one ESP widget. Kept in a single helper so the four
	// possible anchors stay consistent across every element.
	inline void SideSelector( const char * Id, int * StateVar )
	{
		ImGui::PushID( Id );

		ImGui::TextColored( ImColor( 150, 150, 150 ), "%s", xorstr( "Position" ) );
		ImGui::SameLine( );
		ImGui::SetCursorPosX( ImGui::GetCursorPosX( ) + 6.f );

		const char * Sides[ ] = { "Top", "Right", "Bottom", "Left" };
		for ( int i = 0; i < 4; ++i )
		{
			const bool Selected = ( *StateVar == i );

			ImGui::PushStyleColor( ImGuiCol_Button, Selected
				? ImVec4( 0.53f, 0.39f, 1.00f, 0.85f )
				: ImVec4( 0.09f, 0.09f, 0.10f, 1.00f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.53f, 0.39f, 1.00f, 0.45f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.53f, 0.39f, 1.00f, 1.00f ) );

			if ( ImGui::Button( Sides[ i ], ImVec2( 48.f, 18.f ) ) )
			{
				*StateVar = i;
				g_Config.ESP->UpdateCfgESP = true;
			}

			ImGui::PopStyleColor( 3 );

			if ( i < 3 )
				ImGui::SameLine( );
		}

		ImGui::PopID( );
	}

	inline void ColorRow( const char * Id, ImColor * Color )
	{
		ImGui::PushID( Id );

		float Col[ 4 ] = { Color->Value.x, Color->Value.y, Color->Value.z, Color->Value.w };
		if ( ImGui::ColorEdit4( xorstr( "##col" ), Col, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs ) )
		{
			Color->Value.x = Col[ 0 ];
			Color->Value.y = Col[ 1 ];
			Color->Value.z = Col[ 2 ];
			Color->Value.w = Col[ 3 ];
		}

		ImGui::PopID( );
	}

	void Render( ) {


		ImGui::SetCursorPos(ImVec2(490, 50));

		if ( Custom::SubTab( xorstr( "Players" ), 0 == iSubTabCount ) ) {
			iSubTabCount = 0;
		}
		ImGui::SameLine( );
		if ( Custom::SubTab( xorstr( "Vehicles" ), 1 == iSubTabCount ) ) {
			iSubTabCount = 1;
		}
		ImGui::SameLine( );
		if ( Custom::SubTab( xorstr( "Colors" ), 2 == iSubTabCount ) ) {
			iSubTabCount = 2;
		}


		SubTabAlpha = ImClamp( SubTabAlpha + ( 5.f * ImGui::GetIO( ).DeltaTime * ( iSubTabCount == iSubTab ? 1.f : -1.f ) ), 0.f, 1.f );

		if ( SubTabAlpha == 0.f )
			iSubTab = iSubTabCount;


		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, SubTabAlpha * ImGui::GetStyle( ).Alpha );

		ImGui::SetCursorPos( ImVec2(140, 120) );
		ImGui::BeginGroup( );
		{
			switch ( iSubTab )
			{
			case 0: //Players
				ImGui::BeginGroup( );
				{
					ImGui::BeginChild( xorstr( "Globals#ESP" ), ImVec2(500, 340), false, 0 );
					{
						if (Custom::CheckBoxCfg(xorstr("Show"), &g_Config.ESP->Enabled, [&]() {
							static int KeyMode = 2;
							ImGui::Keybind(xorstr("Bind"), &g_Config.ESP->KeyBind, &KeyMode);
							}, false)) {
							g_Config.ESP->FriendsMarker = true;
						}

						Custom::CheckBox(xorstr("Show Preview"), &g_Config.ESP->ShowPreview);
						Custom::CheckBox( xorstr( "Show Box" ), &g_Config.ESP->Box );
						Custom::CheckBox( xorstr( "Show FilledBox" ), &g_Config.ESP->FilledBox );

						Custom::CheckBox( xorstr( "Show Names" ), &g_Config.ESP->UserNames );
						Custom::CheckBox( xorstr( "Show Weapon" ), &g_Config.ESP->WeaponName );
						Custom::CheckBox( xorstr( "Show Distance" ), &g_Config.ESP->DistanceFromMe );

						// Thickness and colour only matter while the skeleton is on,
						// so they live behind the toggle.
						Custom::CheckBoxCfg(xorstr("Show Skeleton"), &g_Config.ESP->Skeleton, [&]() {
							ImGui::SliderFloat(xorstr("Thickness"), &g_Config.ESP->SkeletonThickness, 0.5f, 3.0f, "%.1f");
							ColorRow( xorstr( "SkelCol" ), &g_Config.ESP->SkeletonCol );
							});

						Custom::CheckBox( xorstr( "Show Head" ), &g_Config.ESP->HeadCircle );
						Custom::CheckBox( xorstr( "Show SnapLines" ), &g_Config.ESP->SnapLines );

						// None of these call RemoveKinematics any more - the symbol
						// is not part of CPed and the call was crashing on toggle.
						Custom::CheckBoxCfg(xorstr("Health Bar"), &g_Config.ESP->HealthBar, [&]() {
							SideSelector( xorstr( "HealthSide" ), &g_Config.ESP->HealthBarState );
							ColorRow( xorstr( "HealthCol" ), &g_Config.ESP->HealthCol );
							});

						Custom::CheckBoxCfg(xorstr("Armor Bar"), &g_Config.ESP->ArmorBar, [&]() {
							SideSelector( xorstr( "ArmorSide" ), &g_Config.ESP->ArmorBarState );
							ColorRow( xorstr( "ArmorCol" ), &g_Config.ESP->ArmorCol );
							});

						Custom::CheckBoxCfg(xorstr("Name Style"), &g_Config.ESP->UserNames, [&]() {
							SideSelector( xorstr( "NameSide" ), &g_Config.ESP->UserNamesState );
							ColorRow( xorstr( "NameCol" ), &g_Config.ESP->UserNamesCol );
							});

						Custom::CheckBoxCfg(xorstr("Weapon Style"), &g_Config.ESP->WeaponName, [&]() {
							SideSelector( xorstr( "WeaponSide" ), &g_Config.ESP->WeaponNameState );
							ColorRow( xorstr( "WeaponCol" ), &g_Config.ESP->WeaponNameCol );
							});

						Custom::CheckBoxCfg(xorstr("Distance Style"), &g_Config.ESP->DistanceFromMe, [&]() {
							SideSelector( xorstr( "DistSide" ), &g_Config.ESP->DistanceFromMeState );
							ColorRow( xorstr( "DistCol" ), &g_Config.ESP->DistanceCol );
							});

						Custom::CheckBox( xorstr( "Show LocalPlayer" ), &g_Config.ESP->ShowLocalPlayer );
						Custom::CheckBox( xorstr( "Ignore Dead" ), &g_Config.ESP->IgnoreDead );
						Custom::CheckBox( xorstr( "Ignore NPCs" ), &g_Config.ESP->IgnoreNPCs );

						ImGui::SliderInt( xorstr( "Max Distance" ), &g_Config.ESP->MaxDistance, 0, 1000, xorstr( "%dm" ), 0 );
					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );
				break;
			case 1: //Vehicles
				ImGui::BeginGroup( );
				{
					ImGui::BeginChild( xorstr( "VehGlobals" ), ImVec2(500, 340), false, 0 );
					{
						Custom::CheckBox( xorstr( "Show" ), &g_Config.VehicleESP->Enabled );
						Custom::CheckBox( xorstr( "Show Names" ), &g_Config.VehicleESP->VehName );
						Custom::CheckBox( xorstr( "Locked/Unlocked" ), &g_Config.VehicleESP->ShowLockUnlock );
						Custom::CheckBox( xorstr( "Show SnapLines" ), &g_Config.VehicleESP->SnapLines );
						Custom::CheckBox( xorstr( "Show Distance" ), &g_Config.VehicleESP->DistanceFromMe );
						ColorRow( xorstr( "VehSnapCol" ), &g_Config.VehicleESP->SnapLinesCol );
						ImGui::SliderInt(xorstr("Max Distance"), &g_Config.VehicleESP->MaxDistance, 0, 1000, xorstr("%dm"), 0);
					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );
				break;
			case 2: //Colors
				ImGui::BeginGroup( );
				{
					ImGui::BeginChild( xorstr( "EspColors" ), ImVec2(500, 340), false, 0 );
					{
						ColorRow( xorstr( "BoxCol" ), &g_Config.ESP->BoxCol );
						ColorRow( xorstr( "FilledBoxCol" ), &g_Config.ESP->FilledBoxCol );
						ColorRow( xorstr( "SkeletonCol" ), &g_Config.ESP->SkeletonCol );
						ColorRow( xorstr( "SnapLinesCol" ), &g_Config.ESP->SnapLinesCol );
						ColorRow( xorstr( "FriendCol" ), &g_Config.ESP->FriendCol );
						ColorRow( xorstr( "HealthCol" ), &g_Config.ESP->HealthCol );
						ColorRow( xorstr( "ArmorCol" ), &g_Config.ESP->ArmorCol );
						ColorRow( xorstr( "UserNamesCol" ), &g_Config.ESP->UserNamesCol );
						ColorRow( xorstr( "WeaponNameCol" ), &g_Config.ESP->WeaponNameCol );
						ColorRow( xorstr( "DistanceCol" ), &g_Config.ESP->DistanceCol );
					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );
				break;
			default:
				break;
			}

		}
		ImGui::EndGroup( );
		ImGui::PopStyleVar( );
	}
}
