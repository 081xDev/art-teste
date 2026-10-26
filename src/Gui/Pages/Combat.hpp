	#pragma once
#include <Includes/Includes.hpp>
#include <windows.h>
#include <iostream>
#include <thread>

using namespace std;

namespace Combat {

	static int iSubTabCount = 0;
	static float SubTabAlpha = 0.f;
	static int iSubTab = 0;

	void Render( ) {


		//ImGui::PushStyleVar( ImGuiStyleVar_::ImGuiStyleVar_ItemSpacing, ImVec2( 6, 0 ) );

		ImGui::SetCursorPos(ImVec2(430, 50)); 

		if ( Custom::SubTab( xorstr( "Aimbot" ), 0 == iSubTabCount ) ) {
			iSubTabCount = 0;
		}
		ImGui::SameLine( );
		if ( Custom::SubTab( xorstr( "Trigger" ), 1 == iSubTabCount ) ) {
			iSubTabCount = 1;
		}
		ImGui::SameLine( );
		if ( Custom::SubTab( xorstr( "Silent" ), 2 == iSubTabCount ) ) {
			iSubTabCount = 2;
		}

		SubTabAlpha = ImClamp( SubTabAlpha + ( 5.f * ImGui::GetIO( ).DeltaTime * ( iSubTabCount == iSubTab ? 1.f : -1.f ) ), 0.f, 1.f );

		if ( SubTabAlpha == 0.f )
			iSubTab = iSubTabCount;

		ImGui::PushStyleVar( ImGuiStyleVar_Alpha, SubTabAlpha * ImGui::GetStyle( ).Alpha );

		ImGui::SetCursorPos( ImVec2( 140, 120 ) );
		ImGui::BeginGroup( );
		{
			switch ( iSubTab )
			{
			case 0: //Aimbot
				ImGui::BeginGroup( );
				{
					ImGui::BeginChild( xorstr( "Aimbot" ), ImVec2( 500, 340 ), false, 0 );
					{
						Custom::CheckBox( xorstr( "Show" ), &g_Config.Aimbot->Enabled );

						Custom::CheckBoxCfg(xorstr("Show Fov"), &g_Config.Aimbot->ShowFov, []
							{
								static float AimbotFovCol[4] = { g_Config.Aimbot->FovColor.Value.x, g_Config.Aimbot->FovColor.Value.y, g_Config.Aimbot->FovColor.Value.z,g_Config.Aimbot->FovColor.Value.w }; if (ImGui::ColorEdit4(xorstr("Fov Color"), AimbotFovCol, ImGuiColorEditFlags_AlphaBar)) { g_Config.Aimbot->FovColor.Value.x = AimbotFovCol[0]; g_Config.Aimbot->FovColor.Value.y = AimbotFovCol[1]; g_Config.Aimbot->FovColor.Value.z = AimbotFovCol[2]; g_Config.Aimbot->FovColor.Value.w = AimbotFovCol[3]; }
							}
						);

						Custom::CheckBox( xorstr( "Visible Check" ), &g_Config.Aimbot->OnlyVisible );
						Custom::CheckBox( xorstr( "Ignore NPCs" ), &g_Config.Aimbot->IgnoreNPCs );					
						static int KeyMode = 1;
						ImGui::Keybind(xorstr("KeyBind"), &g_Config.Aimbot->KeyBind, &KeyMode);
						ImGui::SliderInt(xorstr("Fov Size"), &g_Config.Aimbot->FOV, 0, 400, xorstr("%d"), 0);
						ImGui::SliderInt(xorstr("Smoothing"), &g_Config.Aimbot->AimbotSpeed, 0, 100, xorstr("%d"), 0);
						ImGui::SliderInt(xorstr("Max Distance"), &g_Config.Aimbot->MaxDistance, 0, 1000, xorstr("%dm"), 0);;
					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );



				break;
			case 1: //Trigger
				ImGui::BeginGroup( );
				{
					ImGui::BeginChild( xorstr( "Trigger" ), ImVec2(500, 340), false, 0 );
					{
						Custom::CheckBox( xorstr( "Show" ), &g_Config.TriggerBot->Enabled );

						if (!g_Config.TriggerBot->SmartTrigger) {

							Custom::CheckBoxCfg(xorstr("Show Fov"), &g_Config.TriggerBot->ShowFov, []
								{
									static float TriggerFovCol[4] = { g_Config.TriggerBot->FovColor.Value.x, g_Config.TriggerBot->FovColor.Value.y, g_Config.TriggerBot->FovColor.Value.z,g_Config.TriggerBot->FovColor.Value.w };if (ImGui::ColorEdit4(xorstr("Color"), TriggerFovCol, ImGuiColorEditFlags_AlphaBar)){g_Config.TriggerBot->FovColor.Value.x = TriggerFovCol[0];g_Config.TriggerBot->FovColor.Value.y = TriggerFovCol[1];g_Config.TriggerBot->FovColor.Value.z = TriggerFovCol[2];g_Config.TriggerBot->FovColor.Value.w = TriggerFovCol[3];}}
							    );
						}
						else
						{
							g_Config.TriggerBot->ShowFov = false;
						}

						Custom::CheckBox( xorstr( "Smart Triggerbot" ), &g_Config.TriggerBot->SmartTrigger );
						Custom::CheckBox( xorstr( "Visible Check" ), &g_Config.TriggerBot->OnlyVisible );
						Custom::CheckBox( xorstr( "Ignore NPCs" ), &g_Config.TriggerBot->IgnoreNPCs );
						static int KeyMode = 1;
						ImGui::Keybind(xorstr("KeyBind"), &g_Config.TriggerBot->KeyBind, &KeyMode);
						if (!g_Config.TriggerBot->SmartTrigger) {
							ImGui::SliderInt(xorstr("Fov Size"), &g_Config.TriggerBot->FOV, 0, 400, xorstr("%d"), 0);
							ImGui::SliderInt(xorstr("Max Distance"), &g_Config.TriggerBot->MaxDistance, 0, 1000, xorstr("%dm"), 0);
						}

						ImGui::SliderInt(xorstr("Delay"), &g_Config.TriggerBot->Delay, 0, 10, xorstr("%d"), 0);
					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );

				break;
			case 2: //Silent
				ImGui::BeginGroup( );
				{
					ImGui::BeginChild( xorstr( "Silent" ), ImVec2(500, 340), false, 0 );
					{
						Custom::CheckBox( xorstr( "Show" ), &g_Config.SilentAim->Enabled );
						Custom::CheckBoxCfg(xorstr("Show Fov"), &g_Config.SilentAim->ShowFov, []
							{
								static float SilentAimFovCol[4] = { g_Config.SilentAim->FovColor.Value.x, g_Config.SilentAim->FovColor.Value.y, g_Config.SilentAim->FovColor.Value.z,g_Config.SilentAim->FovColor.Value.w }; if (ImGui::ColorEdit4(xorstr("Color"), SilentAimFovCol, ImGuiColorEditFlags_AlphaBar)) { g_Config.SilentAim->FovColor.Value.x = SilentAimFovCol[0]; g_Config.SilentAim->FovColor.Value.y = SilentAimFovCol[1]; g_Config.SilentAim->FovColor.Value.z = SilentAimFovCol[2]; g_Config.SilentAim->FovColor.Value.w = SilentAimFovCol[3]; }}
						);
						Custom::CheckBox( xorstr( "Magic Bullets" ), &g_Config.SilentAim->MagicBullets );
					    Custom::CheckBox( xorstr( "Visible Check" ), &g_Config.SilentAim->OnlyVisible );
						Custom::CheckBox( xorstr( "Ignore NPCs" ), &g_Config.SilentAim->IgnoreNPCs );
						static int KeyMode = 1;
						ImGui::Keybind(xorstr("KeyBind"), &g_Config.SilentAim->KeyBind, &KeyMode);
						ImGui::SliderInt(xorstr("Fov Size"), &g_Config.SilentAim->FOV, 0, 400, xorstr("%d"), 0);
						ImGui::SliderInt(xorstr("Miss Chance"), &g_Config.SilentAim->MissChance, 0, 100, xorstr("%dx"), 0);
						ImGui::SliderInt(xorstr("Max Distance"), &g_Config.SilentAim->MaxDistance, 0, 1000, xorstr("%dm"), 0);
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