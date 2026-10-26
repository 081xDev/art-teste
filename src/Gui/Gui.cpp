#include "Gui.hpp"

#include <Includes/CustomWidgets/Custom.hpp>
#include <Includes/CustomWidgets/WaterMarks.hpp>
#include <Includes/CustomWidgets/Notify.hpp>
#include <Includes/CustomWidgets/Branding.hpp>

#include <Gui/Pages/Combat.hpp>
#include <Gui/Pages/Visuals.hpp>
#include <Gui/Pages/Local.hpp>
#include <Gui/Pages/Settings.hpp>
#include <Gui/Pages/Login.hpp>
#include <Core/Features/Exploits/Exploits.hpp>
#include <Includes/CustomWidgets/Notify.hpp>
#include <Core/Features/Esp.hpp>
#include <Core/Config.hpp>

int PauseLoop;
inline std::mutex DrawMtx;

float accent_color[4] = {
136 / 255.f, // r
100 / 255.f, // g
255 / 255.f, // b
1.f          // a
};

#pragma region Animation










#pragma endregion 


void DrawPreview()
{
	if (!g_Config.ESP || !g_Config.ESP->ShowPreview)
		return;

	static ImVec2 defaultPos = ImVec2(700, 100);
	ImVec2 size = ImVec2(250, 400);

	// Estilo opcional: borda arredondada e fundo escuro
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(12 / 255.f, 12 / 255.f, 14 / 255.f, 1.0f));

	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGui::SetNextWindowPos(defaultPos, ImGuiCond_FirstUseEver);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoTitleBar;

	if (ImGui::Begin("ESP Preview Window", nullptr, flags))
	{
		defaultPos = ImGui::GetWindowPos();

		Custom::g_EspPreview.DragDropHandler();
		Custom::g_EspPreview.Draw();
	}
	ImGui::End();

	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}


void Gui::Rendering()
{
	

	if (!g_MenuInfo.IsLogged)
	{
		g_MenuInfo.MenuSize = { 470, 400 };
	}
	ImGui::SetNextWindowSize(g_MenuInfo.MenuSize);

	if (!PauseLoop) { ImGui::SetNextWindowPos(g_Variables.g_vGameWindowSize / 2 - g_MenuInfo.MenuSize / 2); PauseLoop++; }

	ImGui::Begin(" ", nullptr, ImGuiWindowFlags);
	ImGui::PushFont(g_Variables.m_FontNormal);
	{
		Custom::DrawBackground(g_MenuInfo.IsLogged);
		Custom::Branding::Render(g_MenuInfo.IsLogged);
		if (g_MenuInfo.IsLogged)
		{

		

			ImGui::SetCursorPos(ImVec2(18, 120/*84*/));

			ImGui::BeginGroup();
			ImGui::PushFont(g_Variables.m_FontSecundary);
			{
				
				if (Custom::Tab(ICON_FA_GUN, xorstr("777"), g_MenuInfo.Combat == g_MenuInfo.iTabCount))
				{
					g_MenuInfo.iTabCount = g_MenuInfo.Combat;
				}
				if (Custom::Tab(ICON_FA_EYE, xorstr("555"), g_MenuInfo.Visuals == g_MenuInfo.iTabCount))
				{
					g_MenuInfo.iTabCount = g_MenuInfo.Visuals;
				}
				if (Custom::Tab(ICON_FA_USER, xorstr("444"), g_MenuInfo.Local == g_MenuInfo.iTabCount))
				{
					g_MenuInfo.iTabCount = g_MenuInfo.Local;
				}
				if (Custom::Tab(ICON_FA_GEAR, xorstr("111"), g_MenuInfo.Settings == g_MenuInfo.iTabCount))
				{
					g_MenuInfo.iTabCount = g_MenuInfo.Settings;
				}

			}
			ImGui::PopFont();
			ImGui::EndGroup();
		}






		g_MenuInfo.TabAlpha = ImClamp(g_MenuInfo.TabAlpha + (5.f * ImGui::GetIO().DeltaTime * (g_MenuInfo.iTabCount == g_MenuInfo.iCurrentPage ? 1.f : -1.f)), 0.f, 1.f);

		if (g_MenuInfo.TabAlpha == 0.f)
			g_MenuInfo.iCurrentPage = g_MenuInfo.iTabCount;

		ImGuiStyle* style = &ImGui::GetStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_MenuInfo.TabAlpha * style->Alpha);

		if (g_MenuInfo.IsLogged)
		{


			ImGui::SetCursorPos(ImVec2(176, 16));
			ImGui::BeginGroup();



			switch (g_MenuInfo.iCurrentPage)
			{

			case g_MenuInfo.Combat:
				Combat::Render();
				break;
			case g_MenuInfo.Visuals:
				Visuals::Render();
				break;
			case g_MenuInfo.Local:
				Local::Render();
				break;
			case g_MenuInfo.Settings:
				Settings::Render();
				break;

			}

			DrawPreview();
			ImGui::EndGroup();


			HWND ActiveWindow = GetForegroundWindow();

			{
				std::lock_guard<std::mutex> Lock(DrawMtx);

				NotifyManager::Render();

				if (ActiveWindow == g_Variables.g_hGameWindow)
				{
					if (GetAsyncKeyState(g_Config.Player->GodModeKey) & 1)
					{
						g_Config.Player->EnableGodMode = !g_Config.Player->EnableGodMode;

						if (Core::SDK::Pointers::pLocalPlayer)
							Core::SDK::Pointers::pLocalPlayer->SetGodMode(g_Config.Player->EnableGodMode);

						std::thread([&]()
							{
								NotifyManager::Send(xorstr("GodMode has been ") + (std::string)(g_Config.Player->EnableGodMode ? xorstr("enabled!") : xorstr("disabled!")), 2000);
							}
						).detach();

					}

					if (GetAsyncKeyState(g_Config.ESP->KeyBind) & 1)
					{
						g_Config.ESP->Enabled = !g_Config.ESP->Enabled;

						std::thread([&]()
							{
								NotifyManager::Send(xorstr("ESP has been ") + (std::string)(g_Config.ESP->Enabled ? xorstr("enabled!") : xorstr("disabled!")), 2000);
							}
						).detach();

					}

					if (GetAsyncKeyState(g_Config.Player->NoClipKey) & 1)
					{
						g_Config.Player->NoClipEnabled = !g_Config.Player->NoClipEnabled;

						if (Core::SDK::Pointers::pLocalPlayer)
							Core::SDK::Pointers::pLocalPlayer->FreezePed(g_Config.Player->NoClipEnabled);

						std::thread([&]()
							{
								NotifyManager::Send(xorstr("NoClip has been ") + (std::string)(g_Config.Player->NoClipEnabled ? xorstr("enabled!") : xorstr("disabled!")), 2000);
							}
						).detach();
					}

					if (g_Config.Player->NoClipEnabled && Core::SDK::Pointers::pLocalPlayer)
						Features::Exploits::NoClip();

				}

				if (ActiveWindow == g_Variables.g_hGameWindow || ActiveWindow == g_Variables.g_hCheatWindow)
				{
					struct FovFuncs_t {
						bool* Enabled;
						int* FovSize;
						ImVec4 FovColor;
					};

					std::vector<FovFuncs_t> FovDrawList = {
						FovFuncs_t(&g_Config.Aimbot->ShowFov, &g_Config.Aimbot->FOV, g_Config.Aimbot->FovColor),
						FovFuncs_t(&g_Config.SilentAim->ShowFov, &g_Config.SilentAim->FOV, g_Config.SilentAim->FovColor),
						FovFuncs_t(&g_Config.TriggerBot->ShowFov, &g_Config.TriggerBot->FOV, g_Config.TriggerBot->FovColor),
					};

					static std::vector<float> Alphas(FovDrawList.size(), 0.0f);
					static std::vector<float> Sizes(FovDrawList.size(), 0.0f);

					for (int i = 0; i < FovDrawList.size(); ++i)
					{
						auto& Fov = FovDrawList[i];

						Alphas[i] = ImClamp(ImLerp(Alphas[i], *Fov.Enabled ? 1.f : 0.f, ImGui::GetIO().DeltaTime * 10.f), 0.f, 1.f);
						Sizes[i] = ImLerp(Sizes[i], (float)*Fov.FovSize, ImGui::GetIO().DeltaTime * 12.f);

						ImGui::PushStyleVar(ImGuiStyleVar_Alpha, Alphas[i]);

						ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(g_Variables.g_vGameWindowCenter.x, g_Variables.g_vGameWindowCenter.y), Sizes[i], ImGui::GetColorU32(Fov.FovColor), 999);

						ImGui::PopStyleVar();
					}

					if (g_Config.General->WaterMark)
						Custom::WaterMark::Render();
					if (g_Config.General->ArrayList)
						Custom::ArrayList::Render();
				

					ImGui::PushFont(g_Variables.m_DrawFont);

					Features::g_Esp.Draw();
					Features::g_Esp.DrawVehicle();

					ImGui::PopFont();
				}
				else {

					if (ImGui::GetStyle().Alpha >= 0.9f)
					{
						g_MenuInfo.IsOpen = false;
						SetWindowLong(g_Variables.g_hCheatWindow, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT);
					}

				}

			}
		}
		ImGui::PopStyleVar();
		ImGui::PopFont();
	}
	ImGui::End();
}