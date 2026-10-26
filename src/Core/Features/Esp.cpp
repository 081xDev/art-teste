#include "Esp.hpp"

// ---------------------------------------------------------------------------
// Esp.cpp - player + vehicle overlay
//
// Layout model: every text element resolves its draw position against a small
// cluster (TOP / RIGHT / BOTTOM / LEFT) before drawing, so toggling one element
// never teleports the others. Bars claim their lane before text is placed.
//
// Robustness rules applied here:
//  * No `continue` after partial drawing - a skeleton failure must not kill
//    health/armor/name/weapon/distance.
//  * The animation map is keyed by ped and pruned, so it cannot grow forever.
//  * GameClasses accessors return null on a dead ped; all are checked.
//  * The entity/vehicle vectors are copied under their mutex - the update
//    threads resize them concurrently with the render thread.
// ---------------------------------------------------------------------------

namespace
{
	// Anchor side for a widget. Mirrors Core::Config::ESP::*State values.
	enum EEspSide : int
	{
		ESP_TOP = 0,
		ESP_RIGHT = 1,
		ESP_BOTTOM = 2,
		ESP_LEFT = 3
	};

	struct EspCluster_t
	{
		float Top = 0.f;
		float Bottom = 0.f;
		float Right = 0.f;
		float Left = 0.f;

		void Reset( float box_min_x, float box_max_x, float box_min_y, float box_max_y )
		{
			Top = box_min_y - 4.f;
			Bottom = box_max_y + 4.f;
			Right = box_max_x + 6.f;
			Left = box_min_x - 6.f;
		}
	};

	struct EspLine_t
	{
		ImVec2 Pos{ 0.f, 0.f };
	};

	// Consumes one line slot on the requested side and returns the anchor.
	inline bool PlaceEspText( EspCluster_t & Cluster, int Side, const ImVec2 & BoxCenter,
	                          const ImVec2 & TextSize, EspLine_t & Out )
	{
		switch ( Side )
		{
		case ESP_TOP:
			Out.Pos = ImVec2( BoxCenter.x - ( TextSize.x * 0.5f ), Cluster.Top - TextSize.y );
			Cluster.Top -= ( TextSize.y + 1.f );
			break;

		case ESP_BOTTOM:
			Out.Pos = ImVec2( BoxCenter.x - ( TextSize.x * 0.5f ), Cluster.Bottom );
			Cluster.Bottom += ( TextSize.y + 1.f );
			break;

		case ESP_RIGHT:
			Out.Pos = ImVec2( Cluster.Right, BoxCenter.y - ( TextSize.y * 0.5f ) );
			Cluster.Right += ( TextSize.x + 6.f );
			break;

		case ESP_LEFT:
			Out.Pos = ImVec2( Cluster.Left - TextSize.x, BoxCenter.y - ( TextSize.y * 0.5f ) );
			Cluster.Left -= ( TextSize.x + 6.f );
			break;

		default:
			return false;
		}

		return true;
	}

	inline void DrawOutlinedText( ImDrawList * DrawList, ImFont * Font, float FontSize,
	                              const ImVec2 & Pos, ImColor Color, const char * Text )
	{
		DrawList->AddText( Font, FontSize, ImVec2( Pos.x + 1.f, Pos.y + 1.f ),
		                   ImColor( 0.f, 0.f, 0.f, Color.Value.w ), Text );
		DrawList->AddText( Font, FontSize, Pos, Color, Text );
	}

	inline std::string CapitalizeFirst( const std::string & In )
	{
		std::string Out = In;
		if ( !Out.empty( ) )
			Out[ 0 ] = static_cast< char >( std::toupper( static_cast< unsigned char >( Out[ 0 ] ) ) );
		return Out;
	}

	inline ImColor HealthColor( float Health, float MaxHealth )
	{
		const float Half = ( MaxHealth > 0.f ) ? ( MaxHealth * 0.5f ) : 100.f;

		if ( Health > Half )
			return ImColor( 66, 245, 132, 255 );

		if ( Health > ( Half * 0.5f ) )
			return ImColor( 245, 135, 66, 255 );

		return ImColor( 245, 66, 66, 255 );
	}

	inline ImColor ArmorColor( float Armor, float MaxArmor )
	{
		const float Half = ( MaxArmor > 0.f ) ? ( MaxArmor * 0.5f ) : 50.f;

		if ( Armor > Half )
			return ImColor( 85, 128, 200, 255 );

		if ( Armor > ( Half * 0.5f ) )
			return ImColor( 69, 102, 157, 255 );

		return ImColor( 45, 62, 92, 255 );
	}

	// Vertical bar fills upward from Pos, horizontal bar fills rightward.
	inline void DrawBarV( ImDrawList * DrawList, const ImVec2 & Pos, float Thickness,
	                      float Full, float Amount, ImColor Color )
	{
		if ( Amount <= 0.f || Full <= 0.f )
			return;

		DrawList->AddRectFilled( ImVec2( Pos.x - 1.f, Pos.y + 1.f ),
		                         ImVec2( Pos.x + Thickness + 1.f, Pos.y - Full - 1.f ),
		                         ImColor( 0, 0, 0, 220 ) );
		DrawList->AddRectFilled( ImVec2( Pos.x, Pos.y ),
		                         ImVec2( Pos.x + Thickness, Pos.y - Full ),
		                         ImColor( 80, 80, 80, 160 ) );
		DrawList->AddRectFilled( ImVec2( Pos.x, Pos.y ),
		                         ImVec2( Pos.x + Thickness, Pos.y - Amount ),
		                         Color );
	}

	inline void DrawBarH( ImDrawList * DrawList, const ImVec2 & Pos, float Thickness,
	                      float Full, float Amount, ImColor Color )
	{
		if ( Amount <= 0.f || Full <= 0.f )
			return;

		DrawList->AddRectFilled( ImVec2( Pos.x - 1.f, Pos.y - 1.f ),
		                         ImVec2( Pos.x + Full + 1.f, Pos.y + Thickness + 1.f ),
		                         ImColor( 0, 0, 0, 220 ) );
		DrawList->AddRectFilled( ImVec2( Pos.x, Pos.y ),
		                         ImVec2( Pos.x + Full, Pos.y + Thickness ),
		                         ImColor( 80, 80, 80, 160 ) );
		DrawList->AddRectFilled( ImVec2( Pos.x, Pos.y ),
		                         ImVec2( Pos.x + Amount, Pos.y + Thickness ),
		                         Color );
	}
}

void Core::Features::cEsp::Draw( )
{
	auto Cfg = g_Config.ESP;

	if ( !Cfg || !Cfg->Enabled || Cfg->MaxDistance <= 0 )
		return;

	if ( !Core::SDK::Pointers::pLocalPlayer )
		return;

	ImDrawList * DrawList = ImGui::GetBackgroundDrawList( );
	if ( !DrawList )
		return;

	static std::unordered_map<CPed *, Core::SDK::Game::EspAnim> EspAnimations;

	const ImVec2 ScreenCenter = g_Variables.g_vGameWindowCenter;
	const ImVec2 ScreenSize = g_Variables.g_vGameWindowSize;

	// Snapshot under lock: the entity thread resizes this vector every tick.
	std::vector<Core::SDK::Game::EntityStruct> Entities;
	{
		std::lock_guard<std::mutex> Guard( Core::SDK::Game::EntityListMutex );
		Entities = Core::SDK::Game::EntityList;
	}

	ImFont * DrawFont = g_Variables.m_DrawFont ? g_Variables.m_DrawFont : ImGui::GetFont( );
	const float DrawFontSize = DrawFont->FontSize;

	for ( auto & Entity : Entities )
	{
		CPed * Ped = Entity.Ped;
		if ( !Ped )
			continue;

		const bool IsLocalPlayer = ( Ped == Core::SDK::Pointers::pLocalPlayer );

		if ( !Cfg->ShowLocalPlayer && IsLocalPlayer )
			continue;

		const float Distance = Entity.Distance;
		const float Health = Entity.Health;
		const float MaxHealth = ( Entity.MaxHealth > 0.f ) ? Entity.MaxHealth : 200.f;
		const float Armor = Entity.Armor;
		const int PedType = Entity.PedType;
		bool IsFriend = Entity.IsFriend;

		if ( Distance > static_cast< float >( Cfg->MaxDistance ) )
			continue;

		if ( Cfg->IgnoreNPCs && PedType != Core::SDK::Game::kPedTypePlayer && !IsLocalPlayer )
			continue;

		if ( Cfg->IgnoreDead && !IsLocalPlayer )
		{
			// Players idle at ~100+ health, NPCs at ~1+. Dead reads 0.
			// The population test uses the shared constant rather than a
			// literal, so calibrating the player ped type is a one-line change
			// instead of three edits in three files.
			if ( PedType == Core::SDK::Game::kPedTypePlayer )
			{
				if ( Health > 400.f || Health <= 101.f )
					continue;
			}
			else
			{
				if ( Health > 200.f || Health <= 1.f )
					continue;
			}
		}

		const D3DXVECTOR3 HeadWorld = Ped->GetBonePosDefault( 0 );
		const D3DXVECTOR3 PelvisWorld = Ped->GetBonePosDefault( 8 );

		const D3DXVECTOR2 EntityTop = Core::SDK::Game::WorldToScreen(
			HeadWorld + D3DXVECTOR3( 0.f, 0.f, 0.25f ) );

		// The foot drop distance changed between builds.
		const D3DXVECTOR3 FootOffset = ( g_Offsets.CurrentBuild >= 2802 )
			? D3DXVECTOR3( 0.f, 0.f, 2.0f )
			: D3DXVECTOR3( 0.f, 0.f, 1.2f );

		const D3DXVECTOR2 EntityBottom = Core::SDK::Game::WorldToScreen( PelvisWorld - FootOffset );

		if ( !Core::SDK::Game::IsOnScreen( EntityTop ) || !Core::SDK::Game::IsOnScreen( EntityBottom ) )
			continue;

		const float Height = EntityBottom.y - EntityTop.y;
		if ( Height <= 0.f || Height > 2500.f )
			continue;

		const float Width = Height * 0.2f;
		const float BoxLeft = EntityTop.x - Width;
		const float BoxRight = EntityBottom.x + Width;

		const ImVec2 BoxMin( BoxLeft, EntityTop.y );
		const ImVec2 BoxMax( BoxRight, EntityBottom.y );
		const ImVec2 BoxCenter( ( BoxMin.x + BoxMax.x ) * 0.5f, ( BoxMin.y + BoxMax.y ) * 0.5f );
		const float BarLen = BoxMax.x - BoxMin.x;

		auto & Anim = EspAnimations[ Ped ];

		ImColor BoxCol = Cfg->BoxCol;
		ImColor SkeletonCol = Cfg->SkeletonCol;
		if ( IsFriend )
		{
			BoxCol = Cfg->FriendCol;
			SkeletonCol = Cfg->FriendCol;
		}

		// ---- friend marker / toggle ----
		if ( Cfg->FriendsMarker && !IsLocalPlayer )
		{
			const float MarkerDist = std::hypot( BoxCenter.x - ScreenCenter.x,
			                                     BoxCenter.y - ScreenCenter.y );
			constexpr int FriendFovRadius = 60;
			const int Key = Cfg->FriendsMarkerBind;
			const ImVec2 MarkerPos( BoxCenter.x, BoxCenter.y - 22.f );

			// Bind is held, not tapped - avoids the 300ms polling artifact.
			const bool TogglePressed = ( Key != 0 ) && ( ( GetAsyncKeyState( Key ) & 0x8000 ) != 0 );

			if ( IsFriend )
			{
				DrawList->AddCircleFilled( MarkerPos, 3.f, ImColor( 0, 0, 0 ), 12 );
				DrawList->AddCircleFilled( MarkerPos, 2.f, Cfg->FriendCol, 12 );
				DrawOutlinedText( DrawList, DrawFont, DrawFontSize,
				                  ImVec2( MarkerPos.x + 6.f, MarkerPos.y - ( DrawFontSize * 0.5f ) ),
				                  Cfg->FriendCol, xorstr( "FRIEND" ) );

				if ( TogglePressed )
				{
					IsFriend = false;
					std::lock_guard<std::mutex> Guard( Core::SDK::Game::FriendMapMutex );
					Core::SDK::Game::FriendMap[ Ped ] = false;
				}
			}
			else if ( MarkerDist < FriendFovRadius )
			{
				DrawOutlinedText( DrawList, DrawFont, DrawFontSize,
				                  ImVec2( BoxCenter.x - 24.f, BoxCenter.y - 22.f ),
				                  ImColor( 180, 180, 180, 170 ), xorstr( "look + bind to add" ) );

				if ( TogglePressed )
				{
					IsFriend = true;
					std::lock_guard<std::mutex> Guard( Core::SDK::Game::FriendMapMutex );
					Core::SDK::Game::FriendMap[ Ped ] = true;
				}
			}
		}

		// ---- snaplines ----
		if ( Cfg->SnapLines )
		{
			const ImVec2 Origin( ScreenCenter.x, g_Variables.g_vGameWindowPos.y + ScreenSize.y );
			const ImVec2 Target( BoxCenter.x, BoxMax.y );
			DrawList->AddLine( Origin, Target, ImColor( 0, 0, 0, 180 ), 2.5f );
			DrawList->AddLine( Origin, Target, Cfg->SnapLinesCol, 1.5f );
		}

		// ---- box ----
		if ( Cfg->FilledBox )
			DrawList->AddRectFilled( BoxMin, BoxMax, Cfg->FilledBoxCol, 0.f, 0 );

		if ( Cfg->Box )
		{
			DrawList->AddRect( ImVec2( BoxMin.x - 1.f, BoxMin.y - 1.f ),
			                   ImVec2( BoxMax.x + 1.f, BoxMax.y + 1.f ),
			                   ImColor( 0, 0, 0, 200 ), 0.f, 0, 1.5f );
			DrawList->AddRect( ImVec2( BoxMin.x + 1.f, BoxMin.y + 1.f ),
			                   ImVec2( BoxMax.x - 1.f, BoxMax.y - 1.f ),
			                   ImColor( 0, 0, 0, 200 ), 0.f, 0, 1.5f );
			DrawList->AddRect( BoxMin, BoxMax, BoxCol, 0.f, 0, 1.5f );
		}

		// ---- head circle ----
		if ( Cfg->HeadCircle )
		{
			const D3DXVECTOR2 HeadScreen = Core::SDK::Game::WorldToScreen( HeadWorld );
			if ( Core::SDK::Game::IsOnScreen( HeadScreen ) )
			{
				// Scale with box height so distant players get a small ring.
				// Parenthesised because windows.h defines max as a macro.
				const float Radius = (std::max)( 2.f, Height / 15.f );
				DrawList->AddCircle( ImVec2( HeadScreen.x, HeadScreen.y ), Radius, SkeletonCol, 24, 1.5f );
			}
		}

		// ---- skeleton ----
		// Scoped so a missing bone cannot skip the widgets below.
		if ( Cfg->Skeleton )
		{
			const uintptr_t FragInstNMGta = Mem.Read<uintptr_t>(
				reinterpret_cast< uintptr_t >( Ped ) + g_Offsets.m_FragInst );
			const uintptr_t SkeletonPtr = FragInstNMGta
				? Mem.Read<uintptr_t>( FragInstNMGta + 0x68 )
				: 0u;

			if ( SkeletonPtr )
			{
				Core::SDK::Game::cSkeleton_t Skeleton;
				Skeleton.m_pSkeleton = Mem.Read<uintptr_t>( SkeletonPtr + 0x178 );
				Skeleton.crSkeletonData.Ptr = Mem.Read<uintptr_t>( Skeleton.m_pSkeleton );
				Skeleton.crSkeletonData.m_Used = Mem.Read<unsigned int>( Skeleton.crSkeletonData.Ptr + 0x1A );
				Skeleton.crSkeletonData.m_NumBones = Mem.Read<unsigned int>( Skeleton.crSkeletonData.Ptr + 0x5E );
				Skeleton.crSkeletonData.m_BoneIdTable_Slots = Mem.Read<unsigned short>( Skeleton.crSkeletonData.Ptr + 0x18 );

				if ( Skeleton.crSkeletonData.Ptr && Skeleton.crSkeletonData.m_BoneIdTable_Slots )
				{
					Skeleton.crSkeletonData.m_BoneIdTable = Mem.Read<uintptr_t>( Skeleton.crSkeletonData.Ptr + 0x10 );
					Skeleton.Arg1 = Mem.Read<D3DXMATRIX>( Mem.Read<uintptr_t>( Skeleton.m_pSkeleton + 0x8 ) );
					Skeleton.Arg2 = Mem.Read<uintptr_t>( Skeleton.m_pSkeleton + 0x18 );

					const unsigned int BoneMask[ ] = {
						SKEL_Pelvis, SKEL_Neck_1,
						SKEL_L_UpperArm, SKEL_R_UpperArm,
						SKEL_L_Forearm, SKEL_R_Forearm,
						SKEL_L_Hand, SKEL_R_Hand,
						SKEL_L_Thigh, SKEL_R_Thigh,
						SKEL_L_Calf, SKEL_R_Calf,
						SKEL_L_Foot, SKEL_R_Foot,
					};

					enum : int { iPelvis, iNeck, iLUpperArm, iRUpperArm, iLForearm, iRForearm,
					             iLHand, iRHand, iLThigh, iRThigh, iLCalf, iRCalf, iLFoot, iRFoot, iBoneCount };

					D3DXVECTOR2 Screen[ iBoneCount ];
					bool AllVisible = true;

					for ( int i = 0; i < iBoneCount; ++i )
					{
						const D3DXVECTOR3 World = Core::SDK::Game::GetBonePosComplex(
							Ped, BoneMask[ i ], Skeleton );
						Screen[ i ] = Core::SDK::Game::WorldToScreen( World );

						if ( !Core::SDK::Game::IsOnScreen( Screen[ i ] ) )
						{
							AllVisible = false;
							break;
						}
					}

					if ( AllVisible )
					{
						const float Stick = Cfg->SkeletonThickness;
						const D3DXVECTOR2 HeadScreen = Core::SDK::Game::WorldToScreen( HeadWorld );

						auto Line = [ & ] ( const D3DXVECTOR2 & A, const D3DXVECTOR2 & B )
						{
							DrawList->AddLine( ImVec2( A.x, A.y ), ImVec2( B.x, B.y ), SkeletonCol, Stick );
						};

						Line( HeadScreen, Screen[ iNeck ] );
						Line( Screen[ iNeck ], Screen[ iPelvis ] );
						Line( Screen[ iLUpperArm ], Screen[ iRUpperArm ] );
						Line( Screen[ iLUpperArm ], Screen[ iLForearm ] );
						Line( Screen[ iLForearm ], Screen[ iLHand ] );
						Line( Screen[ iRUpperArm ], Screen[ iRForearm ] );
						Line( Screen[ iRForearm ], Screen[ iRHand ] );
						Line( Screen[ iPelvis ], Screen[ iLThigh ] );
						Line( Screen[ iPelvis ], Screen[ iRThigh ] );
						Line( Screen[ iLThigh ], Screen[ iLCalf ] );
						Line( Screen[ iLCalf ], Screen[ iLFoot ] );
						Line( Screen[ iRThigh ], Screen[ iRCalf ] );
						Line( Screen[ iRCalf ], Screen[ iRFoot ] );
					}
				}
			}
		}

		// ---- bars claim their lane first ----
		EspCluster_t Cluster;
		Cluster.Reset( BoxMin.x, BoxMax.x, BoxMin.y, BoxMax.y );

		if ( Cfg->HealthBar )
		{
			Anim.Health = ImLerp( Anim.Health, Health, ImGui::GetIO( ).DeltaTime * 4.f );

			const float Ratio = ImClamp( ( MaxHealth > 0.f ) ? ( Anim.Health / MaxHealth ) : 0.f, 0.f, 1.f );
			const ImColor Color = HealthColor( Health, MaxHealth );

			switch ( Cfg->HealthBarState )
			{
			case ESP_TOP:
				DrawBarH( DrawList, ImVec2( BoxMin.x, BoxMin.y - 6.f ), 2.f, BarLen, BarLen * Ratio, Color );
				Cluster.Top -= 10.f;
				break;
			case ESP_RIGHT:
				DrawBarV( DrawList, ImVec2( BoxMax.x + 6.f, BoxMax.y ), 2.f, Height, Height * Ratio, Color );
				Cluster.Right += 8.f;
				break;
			case ESP_BOTTOM:
				DrawBarH( DrawList, ImVec2( BoxMin.x, BoxMax.y + 6.f ), 2.f, BarLen, BarLen * Ratio, Color );
				Cluster.Bottom += 10.f;
				break;
			case ESP_LEFT:
			default:
				DrawBarV( DrawList, ImVec2( BoxMin.x - 6.f, BoxMax.y ), 2.f, Height, Height * Ratio, Color );
				Cluster.Left -= 8.f;
				break;
			}
		}

		if ( Cfg->ArmorBar && Armor > 0.f )
		{
			Anim.Armor = ImLerp( Anim.Armor, Armor, ImGui::GetIO( ).DeltaTime * 4.f );

			const float MaxArmor = MaxHealth * 0.5f;
			const float Ratio = ImClamp( ( MaxArmor > 0.f ) ? ( Anim.Armor / MaxArmor ) : 0.f, 0.f, 1.f );
			const ImColor Color = ArmorColor( Armor, MaxArmor );

			switch ( Cfg->ArmorBarState )
			{
			case ESP_TOP:
				DrawBarH( DrawList, ImVec2( BoxMin.x, BoxMin.y - 6.f ), 2.f, BarLen, BarLen * Ratio, Color );
				Cluster.Top -= 10.f;
				break;
			case ESP_RIGHT:
				DrawBarV( DrawList, ImVec2( BoxMax.x + 6.f, BoxMax.y ), 2.f, Height, Height * Ratio, Color );
				Cluster.Right += 8.f;
				break;
			case ESP_BOTTOM:
				DrawBarH( DrawList, ImVec2( BoxMin.x, BoxMax.y + 6.f ), 2.f, BarLen, BarLen * Ratio, Color );
				Cluster.Bottom += 10.f;
				break;
			case ESP_LEFT:
			default:
				DrawBarV( DrawList, ImVec2( BoxMin.x - 6.f, BoxMax.y ), 2.f, Height, Height * Ratio, Color );
				Cluster.Left -= 8.f;
				break;
			}
		}

		// ---- text widgets ----
		EspLine_t Line;

		if ( Cfg->UserNames )
		{
			std::string PlayerName = Entity.NetworkInfo.UserName.empty( )
				? std::string( xorstr( "Unknown" ) )
				: Entity.NetworkInfo.UserName;

			// One long name must not span the screen.
			if ( PlayerName.size( ) > 24 )
				PlayerName = PlayerName.substr( 0, 21 ) + xorstr( "..." );

			const ImVec2 TextSize = DrawFont->CalcTextSizeA( DrawFontSize, FLT_MAX, 0.f, PlayerName.c_str( ) );
			if ( PlaceEspText( Cluster, Cfg->UserNamesState, BoxCenter, TextSize, Line ) )
				DrawOutlinedText( DrawList, DrawFont, DrawFontSize, Line.Pos, Cfg->UserNamesCol, PlayerName.c_str( ) );
		}

		if ( Cfg->WeaponName )
		{
			const std::string WeaponName = Entity.WeaponName.empty( )
				? std::string( xorstr( "Unarmed" ) )
				: CapitalizeFirst( Entity.WeaponName );

			const ImVec2 TextSize = DrawFont->CalcTextSizeA( DrawFontSize, FLT_MAX, 0.f, WeaponName.c_str( ) );
			if ( PlaceEspText( Cluster, Cfg->WeaponNameState, BoxCenter, TextSize, Line ) )
				DrawOutlinedText( DrawList, DrawFont, DrawFontSize, Line.Pos, Cfg->WeaponNameCol, WeaponName.c_str( ) );
		}

		if ( Cfg->DistanceFromMe )
		{
			const std::string DistStr = std::to_string( static_cast< int >( Distance )) + xorstr( "m" );
			const ImVec2 TextSize = DrawFont->CalcTextSizeA( DrawFontSize, FLT_MAX, 0.f, DistStr.c_str( ) );

			if ( PlaceEspText( Cluster, Cfg->DistanceFromMeState, BoxCenter, TextSize, Line ) )
				DrawOutlinedText( DrawList, DrawFont, DrawFontSize, Line.Pos, Cfg->DistanceCol, DistStr.c_str( ) );
		}
	}

	// Drop animation state for peds that left the list.
	if ( EspAnimations.size( ) > 256 )
	{
		for ( auto It = EspAnimations.begin( ); It != EspAnimations.end( ); )
		{
			const bool Alive = std::any_of( Entities.begin( ), Entities.end( ),
				[ & ] ( const Core::SDK::Game::EntityStruct & E ) { return E.Ped == It->first; } );

			if ( Alive )
				++It;
			else
				It = EspAnimations.erase( It );
		}
	}
}

void Core::Features::cEsp::DrawVehicle( )
{
	auto Cfg = g_Config.VehicleESP;

	if ( !Cfg || !Cfg->Enabled )
		return;

	if ( !Core::SDK::Pointers::pLocalPlayer )
		return;

	ImDrawList * DrawList = ImGui::GetBackgroundDrawList( );
	if ( !DrawList )
		return;

	std::vector<Core::SDK::Game::VehicleStructure> Vehicles;
	{
		std::lock_guard<std::mutex> Guard( Core::SDK::Game::VehicleListMutex );
		Vehicles = Core::SDK::Game::VehicleList;
	}

	ImFont * DrawFont = g_Variables.m_DrawFont ? g_Variables.m_DrawFont : ImGui::GetFont( );
	const float DrawFontSize = DrawFont->FontSize;
	const ImVec2 ScreenCenter = g_Variables.g_vGameWindowCenter;
	const float ScreenBottom = g_Variables.g_vGameWindowPos.y + g_Variables.g_vGameWindowSize.y;

	// The lock key is sampled ONCE per frame, outside the per-vehicle loop.
	// Sampled inside, a single `static bool LockKeyHeld` was shared by every
	// vehicle: pressing L toggled only the first in-range vehicle and left the
	// flag set, so no other vehicle could be toggled until the key was released
	// and pressed again. Worse, vehicles outside LockFovRadius skipped the
	// edge-tracking block entirely, leaving the flag stale across frames.
	constexpr int LKey = 0x4C;
	const bool LockKeyDown = ( GetAsyncKeyState( LKey ) & 0x8000 ) != 0;
	static bool LockKeyWasDown = false;
	const bool LockKeyPressed = LockKeyDown && !LockKeyWasDown;
	LockKeyWasDown = LockKeyDown;

	for ( auto & Entity : Vehicles )
	{
		CVehicle * Vehicle = Entity.Pointer;
		if ( !Vehicle )
			continue;

		const float Distance = Entity.Dist;
		if ( Distance > static_cast< float >( Cfg->MaxDistance ) )
			continue;

		// A vehicle read mid-stream can return a zeroed position; skip cleanly
		// rather than projecting the world origin onto the HUD.
		const D3DXVECTOR3 VehiclePos = Vehicle->GetPos( );
		if ( VehiclePos.x == 0.f && VehiclePos.y == 0.f && VehiclePos.z == 0.f )
			continue;

		const D3DXVECTOR2 VehicleLocation = Core::SDK::Game::WorldToScreen( VehiclePos );
		if ( !Core::SDK::Game::IsOnScreen( VehicleLocation ) )
			continue;

		if ( Cfg->SnapLines )
		{
			const ImVec2 Origin( ScreenCenter.x, ScreenBottom );
			const ImVec2 Target( VehicleLocation.x, VehicleLocation.y + 30.f );
			DrawList->AddLine( Origin, Target, ImColor( 0, 0, 0, 180 ), 2.5f );
			DrawList->AddLine( Origin, Target, Cfg->SnapLinesCol, 1.5f );
		}

		if ( Cfg->VehName && !Entity.Name.empty( ) )
		{
			const ImVec2 TextSize = DrawFont->CalcTextSizeA( DrawFontSize, FLT_MAX, 0.f, Entity.Name.c_str( ) );
			DrawOutlinedText( DrawList, DrawFont, DrawFontSize,
			                  ImVec2( VehicleLocation.x - ( TextSize.x * 0.5f ), VehicleLocation.y + 2.f ),
			                  ImColor( 255, 255, 255, 255 ), Entity.Name.c_str( ) );
		}

		if ( Cfg->ShowLockUnlock )
		{
			const bool IsLocked = Entity.IsLocked;
			constexpr int LockFovRadius = 100;

			const float MarkerDist = std::hypot( VehicleLocation.x - ScreenCenter.x,
			                                     VehicleLocation.y - ScreenCenter.y );

			const ImVec2 MarkerPos( VehicleLocation.x, VehicleLocation.y - 6.f );
			const ImColor MarkerCol = IsLocked
				? ImColor( 250, 72, 62, 255 )
				: ImColor( 102, 255, 133, 255 );

			DrawList->AddCircleFilled( MarkerPos, 4.f, ImColor( 0, 0, 0 ), 12 );
			DrawList->AddCircleFilled( MarkerPos, 3.f, MarkerCol, 12 );

			if ( MarkerDist < LockFovRadius )
			{
				const std::string Prompt = IsLocked
					? std::string( xorstr( "Press L to Unlock" ) )
					: std::string( xorstr( "Press L to Lock" ) );

				const ImVec2 TextSize = DrawFont->CalcTextSizeA( DrawFontSize, FLT_MAX, 0.f, Prompt.c_str( ) );
				DrawOutlinedText( DrawList, DrawFont, DrawFontSize,
				                  ImVec2( VehicleLocation.x - ( TextSize.x * 0.5f ), VehicleLocation.y - 30.f ),
				                  ImColor( 255, 255, 255, 255 ), Prompt.c_str( ) );

				// Toggle fires on the rising edge sampled once for the frame, so
				// each vehicle in range responds to its own press.
				if ( LockKeyPressed )
					Vehicle->DoorState( IsLocked );
			}
		}

		if ( Cfg->DistanceFromMe )
		{
			const std::string DistStr = std::to_string( static_cast< int >( Distance )) + xorstr( "m" );
			const ImVec2 TextSize = DrawFont->CalcTextSizeA( DrawFontSize, FLT_MAX, 0.f, DistStr.c_str( ) );

			DrawOutlinedText( DrawList, DrawFont, DrawFontSize,
			                  ImVec2( VehicleLocation.x - ( TextSize.x * 0.5f ), VehicleLocation.y + 16.f ),
			                  ImColor( 255, 255, 255, 255 ), DistStr.c_str( ) );
		}
	}
}
