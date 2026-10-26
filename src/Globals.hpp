#pragma once
#include <Includes/Includes.hpp>

#include <d3dx11.h>
#include <d3d11.h>
#include <D3DX11tex.h>
#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "D3DX11.lib" )

class cColors {
public:
	ImVec4 Base = ImColor(200, 200, 200); //75, 70, 175 - 130, 151, 248
	ImVec4 Tabs = ImColor(200, 200, 200); //75, 70, 175 - 130, 151, 248
	ImVec4 PrimaryText = ImColor( 170, 170, 170 );
	ImVec4 SecundaryText = ImColor( 74, 74, 74 );

	ImVec4 FeaturesText = ImColor(170, 170, 170);
	ImVec4 SecundaryFeaturesText = ImColor(74, 74, 74);

	ImVec4 BorderCol = ImColor( 20, 20, 20 );
	ImVec4 LinesCol = ImColor( 20, 20, 20 );
	ImVec4 BackgroundCol = ImColor(6, 6, 8, 255);

	ImVec4 ChildCol = ImColor( 12, 12, 14 );
	ImVec4 ChildBorderCol = ImColor(12, 12, 14);

	ImVec4 TitleBar = ImColor(12, 12, 14);
	ImVec4 TitleBarBorder = ImColor(12, 12, 14);

	ImVec4 SideBar = ImColor( 14, 14, 16 );
	ImVec4 SideBarBorder = ImColor( 21, 21, 23 );

	ImVec4 ButtonHovered = ImColor( 180, 180, 180, 100 );
	ImVec4 TestingTest = ImColor(180, 180, 180);

	ImVec4 InputBackground = ImColor( 16, 16, 16, 255 );
	ImVec4 InputBorder = ImColor( 22, 22, 22, 255 );
};

inline cColors g_Col;

class c_globals {
public:
	std::string id;
	std::string version;
	DWORD ProcIdFiveM = 0;
	int Pid = 0;

	// These had no initialiser, so every read before the first write saw
	// whatever was in the BSS or on the stack. The verified flag in particular
	// gated the entity thread on uninitialised memory.
	bool g_bPassedByThisVerify = false;
	uintptr_t g_VerifyLogin = 0;
	bool done = false;
	bool IsOpen = false;

	std::string ServerIp = "192.168.0.25";
	std::string UserName = " ";
	std::string Role = " ";


	ImVec2 TestePos;

	char m_Config[ 6000 ];
	HWND g_hCheatWindow = nullptr;
	HWND g_hGameWindow = nullptr;
	ImVec2 g_vGameWindowSize;
	ImVec2 g_vGameWindowPos;
	ImVec2 g_vGameWindowCenter;

	ImFont * m_FontBig = nullptr;
	ImFont * m_FontBigSmall = nullptr;
	ImFont * m_FontNormal = nullptr;
	ImFont * m_FontSecundary = nullptr;
	ImFont * m_FontSmaller = nullptr;
	ImFont * m_DrawFont = nullptr;
	ImFont * m_Expand = nullptr;

	ImFont * FontAwesomeSolid = nullptr;
	ImFont * FontAwesomeSolidSmall = nullptr;
	ImFont * FontAwesomeRegular = nullptr;
	ImFont * FontAwesomeBrands = nullptr;

	ID3D11ShaderResourceView* backgroud = nullptr;
	ID3D11ShaderResourceView * Logo = nullptr;

	struct FiveM_Peds_t {
		ID3D11ShaderResourceView * A_C_Chimp = nullptr;
		ID3D11ShaderResourceView* a_m_y_skater_01 = nullptr;
	}FiveM_PedsPic;
	
};

inline c_globals g_Variables;

class c_menu {
public:

	enum PAGES {
		Combat,
		Visuals,
		Local,
		Settings,
		Login
	};

	bool IsOpen;

	int iTabCount = 0;
	float TabAlpha = 0.f;
	int iCurrentPage = 6;
	float TabAdd = 0.f;
	bool IsLogged = true;
	char cDiscordId[ 200 ] ;
	char user[30] = ""; 
	char pass[30] = ""; 
	bool login_success = false;
	std::string server_response;

	ImVec2 MenuSize{ 680, 500 };
	bool particles = true;


};

inline c_menu g_MenuInfo;