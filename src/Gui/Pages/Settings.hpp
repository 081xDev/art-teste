#pragma once
#include <Includes/Includes.hpp>
#include <windows.h>
#include <iostream>
#include <thread>
#include <Security/DestructHook.hpp>
#include <Security/ScreenShare.hpp>


using namespace std;

namespace Settings {

	static int iSubTabCount = 0;
	static float SubTabAlpha = 0.f;
	static int iSubTab = 0;


	void Render( ) {


		//ImGui::PushStyleVar( ImGuiStyleVar_::ImGuiStyleVar_ItemSpacing, ImVec2( 6, 0 ) );

		ImGui::SetCursorPos(ImVec2(530, 50));

		if ( Custom::SubTab( xorstr( "Main" ), 0 == iSubTabCount ) ) {
			iSubTabCount = 0;
		}
		ImGui::SameLine( );
		if ( Custom::SubTab( xorstr( "User" ), 1 == iSubTabCount ) ) {
			iSubTabCount = 1;
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
			case 0: //Settings
				ImGui::BeginGroup( );
				{
					ImGui::BeginChild( xorstr( "Globals" ), ImVec2(500, 340), false, 0 );
					{
						Custom::CheckBox(xorstr("Second Monitor"), &g_Config.General->SecondMonitor);
						Custom::CheckBox( xorstr( "VSync" ), &g_Config.General->VSync );
						if (Custom::CheckBox(xorstr("Stream Proof"), &g_Config.General->StreamProof))
						{
							if (g_Config.General->StreamProof)
							{
								// Also excludes the overlay from Desktop Duplication
								// and DXGI capture, with a WDA_MONITOR fallback on
								// builds older than Win10 2004.
								Core::Security::g_ScreenShare.Apply(g_Variables.g_hCheatWindow);
							}
							else
							{								
								Core::Security::g_ScreenShare.Remove(g_Variables.g_hCheatWindow);

								SetWindowPos(
									g_Variables.g_hCheatWindow,
									HWND_TOPMOST,
									0, 0, 0, 0,
									SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
								);

								RedrawWindow(
									g_Variables.g_hCheatWindow,
									NULL,
									NULL,
									RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME
								);
							}
						}
					


						static int KeyMode = 1;
						ImGui::Keybind( xorstr( "Menu Key" ), &g_Config.General->MenuKey, &KeyMode );
					
						if (Custom::Button(xorstr("Export Config"), ImVec2(-1, 30), 0))
						{
							std::thread cfgExport([] {
								std::string CfgMsg = g_Config.SaveCurrentConfig(xorstr("..."));
								NotifyManager::Send(CfgMsg.c_str(), 3000);
								});

							cfgExport.detach();
						}
						if (Custom::Button(xorstr("Import Config"), ImVec2(-1, 30), 0))
						{
							std::thread cfgImport([] { NotifyManager::Send(g_Config.LoadCfg(xorstr("..."), Utils::GetClipboard()).c_str(), 3000); });

							cfgImport.detach();
						}

						if (Custom::Button(xorstr("Cleaner"), ImVec2(-1, 30), 0))
						{
							// Runs the full cleanup now: prefetch, registry,
							// USN journal, event logs and timestomping.
							RunDestructOnce();

							std::thread([]() {
								NotifyManager::Send(xorstr("Destruct starting"), 4000);
								Sleep(5000); // Wait 5s so the notify is visible
								exit(0);     // Exit after the cleanup finishes
								}).detach();
						}

						
						if ( Custom::Button( xorstr( "Unload" ), ImVec2( -1, 30 ), 0 ) ) 
						{
							exit( 0 );
						}
					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );

				break;
			case 1: //Colors
				ImGui::BeginGroup( );
				{
					ImGui::BeginChild( xorstr( "Fivem" ), ImVec2( g_MenuInfo.MenuSize.x - ( 150 + 24 ), 300 ), false, 0 );
					{
						Custom::CheckBoxCfg(xorstr("PlayerMark"), &g_Config.General->WaterMark, [&]() {
							static bool Rainbow = false;
							Custom::CheckBox(xorstr("Rainbow"), &Rainbow);
							});

						Custom::CheckBoxCfg(xorstr("ADM list"), &g_Config.General->ArrayList, [&]() {
							static bool Rainbow = false;
							Custom::CheckBox(xorstr("Rainbow"), &Rainbow);
							});
					}
					ImGui::EndChild( );
				}
				ImGui::EndGroup( );
				ImGui::SameLine( );
				ImGui::BeginGroup( );
				{
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