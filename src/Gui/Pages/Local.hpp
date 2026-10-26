#pragma once
#include <Includes/includes.hpp>
#include <windows.h>
#include <iostream>
#include <thread>

#include <Core/Core.hpp>
#include <Core/Features/Exploits/Exploits.hpp>
#include <Core/Features/Exploits/HandlingEditor.hpp>

using namespace std;

namespace Local {

	static int iSubTabCount = 0;
	static float SubTabAlpha = 0.f;
	static int iSubTab = 0;

	void Render( )
	{
		// Every widget below writes straight into CPed. Until the pointer
		// thread has resolved the local player this is null, and the page is
		// still drawable - so bail out instead of faulting in the render pass.
		if ( !Core::SDK::Pointers::pLocalPlayer )
		{
			ImGui::SetCursorPos( ImVec2( 140, 120 ) );
			ImGui::BeginChild( xorstr( "WaitLocal" ), ImVec2( 500, 340 ), false, 0 );
			ImGui::TextColored( ImColor( 180, 180, 180 ), "%s", xorstr( "Waiting for the game... join a server first." ) );
			ImGui::EndChild( );
			return;
		}

		ImGui::SetCursorPos(ImVec2(330, 50));

		if ( Custom::SubTab( xorstr( "Main" ), 0 == iSubTabCount ) ) {
			iSubTabCount = 0;
		}
		ImGui::SameLine( );
		if ( Custom::SubTab( xorstr( "Vehicles" ), 1 == iSubTabCount ) ) {
			iSubTabCount = 1;
		}
		ImGui::SameLine( );
		if ( Custom::SubTab( xorstr( "Teleports" ), 2 == iSubTabCount ) ) {
			iSubTabCount = 2;
		}
		ImGui::SameLine( );
		if ( Custom::SubTab( xorstr( "Weapons" ), 3 == iSubTabCount ) ) {
			iSubTabCount = 3;
		}

		SubTabAlpha = ImClamp( SubTabAlpha + ( 5.f * ImGui::GetIO( ).DeltaTime * ( iSubTabCount == iSubTab ? 1.f : -1.f ) ), 0.f, 1.f );

		if ( SubTabAlpha == 0.f ) iSubTab = iSubTabCount;

		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, SubTabAlpha * ImGui::GetStyle( ).Alpha );

		ImGui::SetCursorPos( ImVec2(140, 120) );
		ImGui::BeginGroup( );
		{
			switch ( iSubTab )
			{
			case 0: //Main
				ImGui::BeginGroup( );
				{
					ImGui::BeginChild( xorstr( "Player" ), ImVec2(500, 340), false, 0 );
					{

						if ( Custom::CheckBoxCfg( xorstr( "Show GodMode" ), &g_Config.Player->EnableGodMode, [ & ] ( ) { static int KeyMode = 1; ImGui::Keybind( xorstr( "Bind" ), &g_Config.Player->GodModeKey, &KeyMode ); }, true, xorstr( "That function can be detected by server AntiCheat" ), ICON_FA_TRIANGLE_EXCLAMATION ) )
						{
							Core::SDK::Pointers::pLocalPlayer->SetGodMode( g_Config.Player->EnableGodMode );
						}

						if ( Custom::CheckBoxCfg( xorstr( "Show NoClip" ), &g_Config.Player->NoClipEnabled, [ & ] ( ) { static int KeyMode = 1; ImGui::Keybind( xorstr( "Bind" ), &g_Config.Player->NoClipKey, &KeyMode ); ImGui::SliderFloat( xorstr( "Speed" ), &g_Config.Player->NoClipSpeed, 0.1f, 20.f, xorstr( "%1.2fm/s" ) ); }, true, xorstr( "That function can be detected by server AntiCheat" ), ICON_FA_TRIANGLE_EXCLAMATION ) )
						{
							Core::SDK::Pointers::pLocalPlayer->FreezePed( g_Config.Player->NoClipEnabled );
						}

						if ( Custom::CheckBoxCfg( xorstr( "Show SpeedRun" ), &g_Config.Player->FastRun, [ & ] ( ) {
							if ( ImGui::SliderFloat( xorstr( "Speed" ), &g_Config.Player->RunSpeed, 1.f, 10.f, xorstr( "%1.2fm/s" ) ) )
							{
								if ( g_Config.Player->FastRun )
									Core::SDK::Pointers::pLocalPlayer->SetSpeed( g_Config.Player->RunSpeed );
							}
							} ) )
						{
							if ( !g_Config.Player->FastRun ) {
								Core::SDK::Pointers::pLocalPlayer->SetSpeed( 1.f );
							}
						}

						if ( Custom::CheckBox( xorstr( "Show Anti Headshot" ), &g_Config.Player->AntiHSEnabled, true, xorstr( "That function can be detected by server AntiCheat" ), ICON_FA_TRIANGLE_EXCLAMATION ) )
						{
							Core::SDK::Pointers::pLocalPlayer->SetConfigFlag( ePedConfigFlag::NoCriticalHits, g_Config.Player->AntiHSEnabled );
						}

						if ( Custom::CheckBox( xorstr( "Show Shrink" ), &g_Config.Player->ShrinkEnabled, true, xorstr( "That function can be detected by server AntiCheat" ), ICON_FA_TRIANGLE_EXCLAMATION ) )
						{
							Core::SDK::Pointers::pLocalPlayer->SetConfigFlag( ePedConfigFlag::Shrink, g_Config.Player->ShrinkEnabled );
						}

						if ( Custom::CheckBox( xorstr( "Show No RagDoll" ), &g_Config.Player->NoRagDollEnabled ) )
						{
							Core::SDK::Pointers::pLocalPlayer->NoRagDoll( g_Config.Player->NoRagDollEnabled );
						}


						if ( Custom::CheckBox( xorstr( "Steal Car" ), &g_Config.Player->StealCarEnabled ) )
						{
							if ( g_Config.Player->StealCarEnabled )
							{
								Core::SDK::Pointers::pLocalPlayer->SetConfigFlag( ePedConfigFlag::NotAllowedToJackAnyPlayers, false );
								Core::SDK::Pointers::pLocalPlayer->SetConfigFlag( ePedConfigFlag::PlayerCanJackFriendlyPlayers, true );
								Core::SDK::Pointers::pLocalPlayer->SetConfigFlag( ePedConfigFlag::WillJackAnyPlayer, true );
							}
							else
							{
								Core::SDK::Pointers::pLocalPlayer->SetConfigFlag( ePedConfigFlag::NotAllowedToJackAnyPlayers, true );
								Core::SDK::Pointers::pLocalPlayer->SetConfigFlag( ePedConfigFlag::PlayerCanJackFriendlyPlayers, false );
								Core::SDK::Pointers::pLocalPlayer->SetConfigFlag( ePedConfigFlag::WillJackAnyPlayer, false );
							}
						}

						if ( Custom::CheckBox( xorstr( "Show CombatRoll" ), &g_Config.Player->InfiniteCombatRoll ) )
						{
							std::thread InfiniteCombatRoll( [ ] ( ) { Core::SDK::Pointers::pLocalPlayer->SetInfCombatRoll( g_Config.Player->InfiniteCombatRoll ); } );
							InfiniteCombatRoll.detach( );
						}

						if ( Custom::CheckBox( xorstr( "Infinite Stamina" ), &g_Config.Player->InfiniteStamina, true, xorstr( "That function can be detected by server AntiCheat" ), ICON_FA_TRIANGLE_EXCLAMATION ) )
						{
							Core::SDK::Pointers::pLocalPlayer->SetInfStamina( g_Config.Player->InfiniteStamina );
						}

						if ( Custom::CheckBox( xorstr( "Force Weapon Wheel" ), &g_Config.Player->ForceWeaponWheel ) )
						{
							std::thread ForceWeaponWheel( [ ] ( ) { Core::SDK::Pointers::pLocalPlayer->ForceWeaponWheel( g_Config.Player->ForceWeaponWheel ); } );
							ForceWeaponWheel.detach( );
						}

						g_Config.Player->CurrentHealthValue = Core::SDK::Pointers::pLocalPlayer->GetHealth() - 100.f > Core::SDK::Pointers::pLocalPlayer->GetMaxHealth() - 100.f ? Core::SDK::Pointers::pLocalPlayer->GetHealth() - 100.f : Core::SDK::Pointers::pLocalPlayer->GetHealth() - 99.f;
						g_Config.Player->CurrentArmorValue = Core::SDK::Pointers::pLocalPlayer->GetArmor();
						if (ImGui::SliderFloat(xorstr("Health"), &g_Config.Player->CurrentHealthValue, -1, Core::SDK::Pointers::pLocalPlayer->GetMaxHealth(), xorstr("%1.f")))
						{
							Core::SDK::Pointers::pLocalPlayer->SetHealth(g_Config.Player->CurrentHealthValue + 100.f);
						}
						if (ImGui::SliderFloat(xorstr("Armor"), &g_Config.Player->CurrentArmorValue, 0, 100/*MaxArmor*/, xorstr("%1.f")))
						{
							Core::SDK::Pointers::pLocalPlayer->SetArmor(g_Config.Player->CurrentArmorValue);
						}

					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );
				break;
			case 1: //Vehicles
			{
				bool InVehicle = Core::SDK::Pointers::pLocalPlayer->InVehicle( );

				auto CurrentVehicle = Core::SDK::Pointers::pLocalPlayer->GetLastVehicle( );
				ImGui::BeginGroup( );
				{


					ImGui::BeginChild( xorstr( "Vehicle" ), ImVec2(500, 340), false, 0 );
					{
						if ( Custom::CheckBox( xorstr( "Show GodMode" ), &g_Config.Player->VehicleGodMode ) )
						{
							if ( !InVehicle )
							{
								g_Config.Player->VehicleGodMode = false;
								std::thread VehicleAlert( [ ] ( ) {  NotifyManager::Send( xorstr( "You must be in a car to use this function" ), 4000 );  } );
								VehicleAlert.detach( );
							}
							else {
								//Atualizar Offsets
								CurrentVehicle->SetGodMode( g_Config.Player->VehicleGodMode );
							}
						}
						static bool Locked = CurrentVehicle->IsLocked( );
						if ( Custom::CheckBox( xorstr( "Doors Locked" ), &Locked ) )
						{
							CurrentVehicle->DoorState( !Locked );
						}

						if ( Custom::CheckBox( xorstr( "SeatBelt" ), &g_Config.Player->SeatBelt ) )
						{
							if ( !InVehicle )
							{
								g_Config.Player->VehicleGodMode = false;
								std::thread VehicleAlert( [ ] ( ) {  NotifyManager::Send( xorstr( "You must be in a car to use this function" ), 4000 );  } );
								VehicleAlert.detach( );
							}
							else {

								std::thread SeatBelt( [ ] ( ) 
									{
									Core::SDK::Pointers::pLocalPlayer->SeatBealt( g_Config.Player->SeatBelt );
									}
								);
								SeatBelt.detach( );

							}
						}


						if ( Custom::CheckBoxPage( xorstr( "Show Tune" ), &g_Config.Player->HandlingEditor, [ & ] ( ) {
							if ( !InVehicle ) {
								std::thread VehicleAlert( [ ] ( ) {  NotifyManager::Send( xorstr( "You must be in a car to use this function" ), 4000 );  } );
								VehicleAlert.detach( );
							}
							else {
								iSubTabCount = 4;
							}
							}, true, xorstr( "Click here to go for Handling Editor Page" ) ) )
						{
							if ( !InVehicle )
							{
								g_Config.Player->HandlingEditor = false;
								std::thread VehicleAlert( [ ] ( ) {  NotifyManager::Send( xorstr( "You must be in a car to use this function" ), 4000 );  } );
								VehicleAlert.detach( );
							}

							if ( g_Config.Player->HandlingEditor )
							{
								Features::Exploits::g_HandlingEditor.SaveHandlingValues( );
							}
							else
							{
								Features::Exploits::g_HandlingEditor.RestoreHandlingValues( );
							}

						}

					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );
			}
			break;
			case 2: //Teleports
				ImGui::SetCursorPos( ImVec2(140, 120) );
				ImGui::BeginGroup( );
				{
					struct Locations_t {
						std::string Name;
						D3DXVECTOR3 Coords;
					};

					std::vector<Locations_t> Locations = {
						Locations_t( xorstr( "Waypoint" ), D3DXVECTOR3( 0, 0, 0 ) ),
						Locations_t( xorstr( "Square" ), D3DXVECTOR3( 156.184, -1043.17, 29.3236 ) ),
						Locations_t( xorstr( "Pier" ), D3DXVECTOR3( -1847.72, -1223.36, 13.8745 ) ),
						Locations_t( xorstr( "Paleto Bay" ), D3DXVECTOR3( -397.605, 6047.57, 32.1797 ) ),
						Locations_t( xorstr( "Central Bank" ), D3DXVECTOR3( 221.781, 217.278, 106.705 ) ),
						Locations_t( xorstr( "Cassino" ), D3DXVECTOR3( 885.322, 16.8489, 80.65 ) ),
						Locations_t( xorstr( "Los Santos Airport" ), D3DXVECTOR3( -975.532, -2880.89, 16.2665 ) ),
						Locations_t( xorstr( "Sandy Shores" ), D3DXVECTOR3( 1681.48, 3251.91, 40.809 ) ),
					};

					static int SeletedIndex = 0;
					bool IsSelected;

					ImGui::BeginChild( xorstr( "Locations" ), ImVec2( g_MenuInfo.MenuSize.x - (150 + 24), 300 ), false, 0 );
					{
						ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 14, 8 ) );
						for ( int i = 0; i < Locations.size( ); i++ )
						{
							IsSelected = SeletedIndex == i;
							if ( ImGui::ListSelectable( Locations[ i ].Name.c_str( ), &IsSelected ) ) SeletedIndex = i;
						}
						ImGui::PopStyleVar( );
					}
					ImGui::EndChild( );
					static ImVec2 ContentSize = ImVec2( g_MenuInfo.MenuSize.x - (150 + 24), 180 );
					ImGui::BeginContent( xorstr( "LocationsInfo" ), ContentSize, false, 0 );
					{
						ImGui::BeginGroup( );
						{
							auto Location = Locations[ SeletedIndex ];
							std::string Loc = xorstr( "Teleport to " ) + Location.Name;
							if ( Custom::Button( Loc.c_str( ), ImVec2( ContentSize.x, 32 ), 0 ) ) {
								if ( SeletedIndex == 0 )
								{
									Core::Features::Exploits::TpToWaypoint( );
								}
								else {
									Core::SDK::Pointers::pLocalPlayer->SetPos( Location.Coords );
								}
							}
						}
						ImGui::EndGroup( );

					}
					ImGui::EndContent( );


				}
				ImGui::EndGroup( );
				break;
			case 3: //Weapons
				ImGui::BeginGroup( );
				{
					ImGui::BeginChild( xorstr( "Misc" ), ImVec2(500, 340), false, 0 );
					{
						g_Config.Player->RecoilValue = Core::SDK::Pointers::pLocalPlayer->GetWeaponManager( )->GetRecoil( );
						static const float RecoilBackup = g_Config.Player->RecoilValue;

						g_Config.Player->SpreadValue = Core::SDK::Pointers::pLocalPlayer->GetWeaponManager( )->GetSpread( );
						static const float SpreadBackup = g_Config.Player->RecoilValue;

						if ( Custom::CheckBoxCfg( xorstr( "Recoil Control" ), &g_Config.Player->NoRecoilEnabled, [ & ] ( ) {

							if ( ImGui::SliderFloat( xorstr( "Value" ), &g_Config.Player->RecoilValue, 0.0f, 10.f, xorstr( "%1.2fx" ) ) ) {
								if ( g_Config.Player->NoRecoilEnabled ) {
									Core::SDK::Pointers::pLocalPlayer->GetWeaponManager( )->SetRecoil( g_Config.Player->RecoilValue );
								}
							}
							} ) )
						{
							if ( !g_Config.Player->NoRecoilEnabled ) {
								Core::SDK::Pointers::pLocalPlayer->GetWeaponManager( )->SetRecoil( RecoilBackup );
							}
						}

						if ( Custom::CheckBoxCfg( xorstr( "Spread Control" ), &g_Config.Player->NoSpreadEnabled, [ & ] ( ) {
							if ( ImGui::SliderFloat( xorstr( "Value" ), &g_Config.Player->SpreadValue, 0.0f, 10.f, xorstr( "%1.2fx" ) ) ) {
								if ( g_Config.Player->NoSpreadEnabled ) {
									Core::SDK::Pointers::pLocalPlayer->GetWeaponManager( )->SetSpread( g_Config.Player->SpreadValue );
								}
							}
							} ) )
						{
							if ( !g_Config.Player->NoSpreadEnabled ) {
								Core::SDK::Pointers::pLocalPlayer->GetWeaponManager( )->SetSpread( SpreadBackup );
							}
						}


						if ( Custom::CheckBox( xorstr( "No Reload" ), &g_Config.Player->NoReloadEnabled ) )
						{
							if ( g_Config.Player->NoReloadEnabled ) {
								Mem.PatchFunc( g_Offsets.m_InfiniteAmmo0, 3 );
							}
							else {
								Mem.WriteBytes( g_Offsets.m_InfiniteAmmo0, { 0x41, 0x2B, 0xC9, 0x3B, 0xC8, 0x0F, 0x4D, 0xC8 } );
							}

						}

						if ( Custom::CheckBox( xorstr( "Infinite Ammo" ), &g_Config.Player->InfiniteAmmoEnabled ) )
						{
							if ( g_Config.Player->InfiniteAmmoEnabled ) {
								Mem.PatchFunc( g_Offsets.m_InfiniteAmmo0, 3 );
								Mem.PatchFunc( g_Offsets.m_InfiniteAmmo1, 3 );
							}
							else {
								if ( !g_Config.Player->NoReloadEnabled )
								{
									Mem.WriteBytes( g_Offsets.m_InfiniteAmmo0, { 0x41, 0x2B, 0xC9, 0x3B, 0xC8, 0x0F, 0x4D, 0xC8 } );
								}
								Mem.WriteBytes( g_Offsets.m_InfiniteAmmo1, { 0x41, 0x2B, 0xD1, 0xE8 } );
							}

						}

						static int Ammo = 0;
						ImGui::SliderInt(xorstr("Ammo Value"), &Ammo, 0, 250);

						if (Custom::Button(xorstr("Add Ammo"), ImVec2(-1, 33), 0, true, xorstr("That function can be detected by server AntiCheat"), ICON_FA_TRIANGLE_EXCLAMATION))
						{
							CWeaponInfo* pCWeaponInfo = Core::SDK::Pointers::pLocalPlayer->GetWeaponManager()->GetWeaponInfo();
							if (pCWeaponInfo)
							{
								//48 8b 43 ? 48 83 c4 ? 5b c3 cc 40 53
								uintptr_t Lixo = Mem.FindSignature(
									{ 0x48, 0x8b, 0x43, 0x00, 0x48, 0x83, 0xc4, 0x00, 0x5b, 0xc3, 0xcc, 0x40, 0x53 }
								);

								uintptr_t AmmoInfo = Mem.Read<uintptr_t>((uintptr_t)pCWeaponInfo + Mem.Read<BYTE>(Lixo + 3));

								if (AmmoInfo)
								{
									uintptr_t pAmmoCount = Mem.Read<uintptr_t>((uintptr_t)AmmoInfo + 0x8);
									if (pAmmoCount)
									{

										uintptr_t CurrentAmmoAddr = Mem.Read<uintptr_t>(pAmmoCount + 0x0) + 0x18;
										int CurrentAmmo = Mem.Read<int>(CurrentAmmoAddr);

										Mem.Write(CurrentAmmoAddr, CurrentAmmo + Ammo);
									}
								}
								else
								{
									std::thread AmmoAlert([]() {  NotifyManager::Send(xorstr("Get a weapon first!"), 4000);  });
									AmmoAlert.detach();
								}
							}
						}

					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );
				break;
			case 4: //HandlingEditor
				ImGui::BeginGroup( );
				{
					bool InVehicle = Core::SDK::Pointers::pLocalPlayer->InVehicle( );

					if ( !InVehicle ) 
						iSubTabCount = 1;

					ImGui::SetCursorPos( ImVec2( 184, 60 ) );

					ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 10, 4 ) );
					{
						ImGui::TextColored( g_Col.FeaturesText, xorstr( "Handling Editor" ) );
						ImGui::PushFont( g_Variables.FontAwesomeSolidSmall );
						{
							ImGui::SameLine( );
							ImGui::TextColored( g_Col.Base, ICON_FA_REPLY );
							if ( ImGui::IsItemClicked( ) )
							{
								iSubTabCount = 1;
							}
						}
						ImGui::PopFont( );
						ImGui::PushFont( g_Variables.m_FontSecundary );
						{
							ImGui::TextColored( g_Col.SecundaryText, xorstr( "Here you can edit values from handling of your Vehicle." ) );
						}
						ImGui::PopFont( );
					}
					ImGui::PopStyleVar( );

					ImGui::Spacing( );

					ImGui::BeginChild( xorstr( "Handling" ), ImVec2( g_MenuInfo.MenuSize.x - ( 184 + 24 ), 360 ), false, 0 );
					{

						if ( ImGui::SliderFloat( xorstr( "Acceleration" ), &Features::Exploits::g_HandlingEditor.fAcceleration, 0.0f, 400.f, xorstr( "%1.1f" ) ) )
						{
							Features::Exploits::g_HandlingEditor.ApplyHandlingValues( );
						}

						if (ImGui::SliderFloat(xorstr("Break Force"), &Features::Exploits::g_HandlingEditor.fBreakForce, 0.0f, 100.f, xorstr("%1.1f")))
						{
							Features::Exploits::g_HandlingEditor.ApplyHandlingValues();
						}

						if ( ImGui::SliderFloat( xorstr( "Traction Curve Min" ), &Features::Exploits::g_HandlingEditor.fTractionCurveMin, 0.0f, 100.f, xorstr( "%1.1f" ) ) )
						{
							Features::Exploits::g_HandlingEditor.ApplyHandlingValues( );
						}

					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );
				break;
			case 5: //WeaponSpawn
				ImGui::BeginGroup( );
				{
					ImGui::SetCursorPos( ImVec2( 105, 60 ) );

					ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 10, 4 ) );
					{
						ImGui::TextColored( g_Col.FeaturesText, xorstr( "Weapon Spawn" ) );
						ImGui::PushFont( g_Variables.FontAwesomeSolidSmall );
						{
							ImGui::SameLine( );
							ImGui::TextColored( g_Col.Base, ICON_FA_REPLY );
							if ( ImGui::IsItemClicked( ) ) {
								iSubTabCount = 3;
							}
						}
						ImGui::PopFont( );
						ImGui::PushFont( g_Variables.m_FontSecundary );
						{
							ImGui::TextColored( g_Col.SecundaryText, xorstr( "Here you can spawn weapon for you :)" ) );
						}
						ImGui::PopFont( );
					}
					ImGui::PopStyleVar( );

					ImGui::Spacing( );

					ImGui::BeginContent( xorstr( "Peds Tab" ), ImVec2( g_MenuInfo.MenuSize.x - ( 188 + 24 ), 360 ), 0, 0 );
					{
						struct SetPed_t {
							std::string Name;
							unsigned int WeaponHash;
							ID3D11ShaderResourceView * Picture;
						};

						std::vector<SetPed_t> PedList = {

							SetPed_t(xorstr("A_C_Chimp"), 0xA8683715, g_Variables.FiveM_PedsPic.A_C_Chimp),
							SetPed_t(xorstr("a_m_y_skater_01"), 0xA8683715, g_Variables.FiveM_PedsPic.a_m_y_skater_01),
						
						};

						int count = 0;
						//ImGui::SetCursorPosX( 6 );
						for ( auto Ped : PedList )
						{

							if (Ped.Name.empty( ) || Ped.Picture == nullptr ) {
								continue;
							}

							if ( Custom::PedButtonHeld(Ped.Picture, Ped.Name.c_str( ), 0 ) ) {
								std::string Msg = Ped.Name + xorstr( " Setado." );
								std::thread NotifyThread( NotifyManager::Send, Msg, 4000 );
								NotifyThread.detach( );
							}

							count++;

							if ( count % 4 != 0 ) {
								ImGui::SameLine( );
							}
							else {
								count = 0;
								//ImGui::SetCursorPosX( 6 );
							}
						}
					}
					ImGui::EndContent( );
				}
				ImGui::EndGroup( );
			default:
				break;
			}

		}
		ImGui::EndGroup( );
		ImGui::PopStyleVar( );

	}
}