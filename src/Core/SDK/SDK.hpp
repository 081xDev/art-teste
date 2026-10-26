#pragma once
#include <Includes/Includes.hpp>
#include <Core/SDK/Memory.hpp>
#include <Core/Offsets.hpp>
#include <Core/SDK/Structs/GameClasses.hpp>


#include <d3dx11.h>
#include <d3d11.h>
#include <D3DX11tex.h>
#include <D3dx9math.h>
#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "D3DX11.lib" )

namespace Core {

	namespace SDK {

		namespace Pointers {
			// Object Pointers
			inline CPedFactory * pWorld = nullptr;
			inline CPed * pLocalPlayer = nullptr;
			inline CReplayInterFace * pReplayInterFace = nullptr;
			inline uintptr_t pBlipList = 0, pViewPort = 0, pCamGamePlayDirector = 0;
		}

		namespace Game
		{
			// The CPed type value that identifies a player-controlled ped.
			//
			// GetPedType() returns the raw CPed type field. This literal was
			// previously duplicated across Esp.cpp (twice) and GetClosestPed
			// (once) with no single source of truth, which is exactly how an
			// inverted test survives: if the real player value is 1 rather than
			// 2, then IgnoreNPCs skips every real player and aims only at NPCs,
			// and the IgnoreDead health bands apply to the wrong population.
			//
			// Confirm against the target build by enabling IgnoreNPCs and
			// checking whether other players remain on screen. If they vanish,
			// change this one value.
			constexpr int kPedTypePlayer = 2;

			struct cSkeleton_t {

				std::unordered_map<unsigned int, int> StoredBonesIdx;

				struct crSkeletonData_t {
					uintptr_t Ptr, m_BoneIdTable;
					unsigned int m_Used; // crSkeletonData->m_BoneIdTable.m_Used
					unsigned int m_NumBones; // crSkeletonData->m_NumBones
					unsigned short m_BoneIdTable_Slots;
				} crSkeletonData;

				uintptr_t m_pSkeleton, Arg2;
				D3DXMATRIX Arg1;
			};

			struct NetworkInfo {
				std::string UserName;
				std::string DiscordId;
				std::string SteamId;
			};

			struct EntityStruct {
				CPed * Ped;

				int Id;
				int Index;
				int PedType;

				bool IsFriend;

				D3DXVECTOR3 Pos;

				float Health;
				float MaxHealth;
				float Armor;
				float Distance;

				std::string WeaponName;

				NetworkInfo NetworkInfo;

				float HealthAnim;
			};

			struct EspAnim {
				bool CanFadeOut;
				float Health;
				float Armor;
				float Alpha;
			};

			struct VehicleStructure {
				CVehicle * Pointer;
				CPed * Driver;
				std::string Name;
				float Dist;
				bool IsLocked;
				D3DXVECTOR3 Pos;
			};



			// Written by the entity/vehicle/name threads, read by the render
			// thread. Every cross-thread access must take the matching mutex.
			inline std::vector<EntityStruct> EntityList;
			inline std::mutex EntityListMutex;

			inline std::vector<VehicleStructure> VehicleList;
			inline std::mutex VehicleListMutex;

			inline std::unordered_map<CPed *, bool> FriendMap;
			inline std::mutex FriendMapMutex;

			inline D3DXVECTOR2 WorldToScreen( D3DXVECTOR3 World )
			{
				D3DXMATRIX ViewMatrix = Mem.Read<D3DXMATRIX>( Core::SDK::Pointers::pViewPort + 0x24C );
				D3DXMatrixTranspose( &ViewMatrix, &ViewMatrix );

				auto VecX = D3DXVECTOR4( ViewMatrix._21, ViewMatrix._22, ViewMatrix._23, ViewMatrix._24 ),
					VecY = D3DXVECTOR4( ViewMatrix._31, ViewMatrix._32, ViewMatrix._33, ViewMatrix._34 ),
					VecZ = D3DXVECTOR4( ViewMatrix._41, ViewMatrix._42, ViewMatrix._43, ViewMatrix._44 );

				D3DXVECTOR3 ScreenPos = D3DXVECTOR3(
					( VecX.x * World.x ) + ( VecX.y * World.y ) + ( VecX.z * World.z ) + VecX.w,
					( VecY.x * World.x ) + ( VecY.y * World.y ) + ( VecY.z * World.z ) + VecY.w,
					( VecZ.x * World.x ) + ( VecZ.y * World.y ) + ( VecZ.z * World.z ) + VecZ.w
				);

				if ( ScreenPos.z <= 0.1f )
					return D3DXVECTOR2( 0, 0 );

				ScreenPos.z = 1.0f / ScreenPos.z;
				ScreenPos.x *= ScreenPos.z;
				ScreenPos.y *= ScreenPos.z;

				ScreenPos.x += ( g_Variables.g_vGameWindowSize.x / 2 ) + float( 0.5f * ScreenPos.x * g_Variables.g_vGameWindowSize.x + 0.5f );
				ScreenPos.y = ( g_Variables.g_vGameWindowSize.y / 2 ) - float( 0.5f * ScreenPos.y * g_Variables.g_vGameWindowSize.y + 0.5f );
				return D3DXVECTOR2( ScreenPos.x, ScreenPos.y );
			}

			inline std::unordered_map<uintptr_t, std::unordered_map<unsigned int, int>> StoredBonesIdx;
			inline std::mutex BonesCache;


			inline bool GetBoneIndex( cSkeleton_t cSkeleton, unsigned int boneId, int & outBoneIdx )
			{
				std::lock_guard<std::mutex> lock( BonesCache );
				{
					auto StoredBonesIdxCache = StoredBonesIdx[ cSkeleton.crSkeletonData.Ptr ];

					if ( StoredBonesIdxCache.find( boneId ) != StoredBonesIdxCache.end( ) ) {
						outBoneIdx = StoredBonesIdxCache[ boneId ];
						return true;
					}


					if ( cSkeleton.crSkeletonData.m_Used != 0 ) // crSkeletonData->m_BoneIdTable.m_Used
					{
						unsigned short m_BoneIdTable_Slots = cSkeleton.crSkeletonData.m_BoneIdTable_Slots; // crSkeletonData->m_BoneIdTable.m_Slots

						std::uintptr_t m_BoneIdTable_Hash = Mem.Read<std::uintptr_t>( cSkeleton.crSkeletonData.m_BoneIdTable + 0x8 * ( boneId % ( unsigned int ) m_BoneIdTable_Slots ) );
						for ( std::uintptr_t i = m_BoneIdTable_Hash; i != 0; i = Mem.Read<std::uintptr_t>( i + 0x8 ) )
						{
							int i_key = Mem.Read<int>( i );
							if ( boneId == i_key )
							{
								int p_Data = Mem.Read<int>( i + 0x4 );
								if ( p_Data )
								{
									outBoneIdx = p_Data;
									StoredBonesIdxCache[ boneId ] = p_Data;
									return true;
								}
							}
						}
					}
					else if ( boneId < cSkeleton.crSkeletonData.m_NumBones )
					{
						outBoneIdx = boneId;
						StoredBonesIdxCache[ boneId ] = boneId;

						return true;
					}

					return false;
				}
			}

			inline D3DXVECTOR3 GetBonePosInstFrag( std::uintptr_t InstFrag, unsigned int BoneID, D3DXMATRIX Arg1, uintptr_t Arg2 )
			{
				D3DXMATRIX v4; // r8
				D3DXMATRIX Result; // rax

				v4 = Arg1;
				if ( !v4 ) { return D3DXVECTOR3( 0, 0, 0 ); }

				Result = Mem.Read<D3DXMATRIX>( Arg2 + ( ( unsigned __int64 ) BoneID << 6 ) );
				if ( !Result ) { return D3DXVECTOR3( 0, 0, 0 ); }

				D3DXVECTOR3 vec1( v4._11, v4._12, v4._13 );
				D3DXVECTOR3 vec2( v4._21, v4._22, v4._23 );
				D3DXVECTOR3 vec3( v4._31, v4._32, v4._33 );
				D3DXVECTOR3 vec4( v4._41, v4._42, v4._43 );
				D3DXVECTOR3 vec5( Result._41, Result._42, Result._43 );
				return D3DXVECTOR3(
					vec1.x * vec5.x + vec4.x + vec2.x * vec5.y + vec3.x * vec5.z,
					vec1.y * vec5.x + vec4.y + vec2.y * vec5.y + vec3.y * vec5.z,
					vec1.z * vec5.x + vec4.z + vec2.z * vec5.y + vec3.z * vec5.z
				);
			}

			inline D3DXVECTOR3 GetBonePosComplex( CPed * Ped, unsigned int Mask, cSkeleton_t cSkeleton_t )
			{
				bool result = false;

				int BoneId = 0;

				if ( GetBoneIndex( cSkeleton_t, Mask, BoneId ) ) {
					return GetBonePosInstFrag( cSkeleton_t.m_pSkeleton, BoneId, cSkeleton_t.Arg1, cSkeleton_t.Arg2 );
				}

				return Ped->GetPos( );
			}

			inline std::string GetPedName( /*int id*/ CPed * Ped )
			{
				if ( !Ped )
					return std::string( "Unknown" );

				// Iterate a snapshot - the entity thread rebuilds this vector
				// while the render thread walks it.
				std::vector<EntityStruct> Snapshot;
				{
					std::lock_guard<std::mutex> Guard( EntityListMutex );
					Snapshot = EntityList;
				}

				for ( const auto & Ent : Snapshot )
				{
					if ( Ent.Ped != Ped )
						continue;

					return Ent.NetworkInfo.UserName.empty( )
						? std::string( "Unknown" )
						: Ent.NetworkInfo.UserName;
				}

				// Previously fell off the end of the function.
				return std::string( "Unknown" );
			}

			// Forward declarations: both are defined below, but GetClosestPed
			// calls them and these are inline free functions, so the names must
			// be visible before its body.
			inline bool IsOnScreen( D3DXVECTOR2 Pos );
			inline bool IsEntityVisible( CPed * Ped );

			inline CPed* GetClosestPed(int MaxDistance, bool IgnoreNpc, bool VisibleCheck)
			{
				CPed* ClosestPed = nullptr;
				float ClosestDist = FLT_MAX;

				std::vector<EntityStruct> Snapshot;
				{
					std::lock_guard<std::mutex> Guard( EntityListMutex );
					Snapshot = EntityList;
				}

				for (auto& Entity : Snapshot)
				{
					CPed* Ped = Entity.Ped;

					if (Ped == Core::SDK::Pointers::pLocalPlayer)
						continue;

					if (std::abs(Entity.Health) <= 101.f)
						continue;

					if (IgnoreNpc && Entity.PedType != kPedTypePlayer)
						continue;

					if (Entity.IsFriend)
						continue;

					if (Entity.Distance > MaxDistance)
						continue;

					// VisibleCheck was accepted and then ignored, so "visible
					// only" silently aimed through walls.
					if (VisibleCheck && !Core::SDK::Game::IsEntityVisible(Ped))
						continue;

					D3DXVECTOR2 PedPos = Core::SDK::Game::WorldToScreen(Entity.Pos);
					if (!Core::SDK::Game::IsOnScreen(PedPos))
						continue;

					float x = PedPos.x - g_Variables.g_vGameWindowCenter.x;
					float y = PedPos.y - g_Variables.g_vGameWindowCenter.y;
					float AimDist = std::sqrt((x * x) + (y * y));

					if (AimDist < ClosestDist)
					{
						ClosestDist = AimDist;
						ClosestPed = Ped;
					}
				}

				return ClosestPed;
			}


			// -----------------------------------------------------------------
			// Thread suspension for live code patching.
			//
			// Writing the 14-byte hook in SilentAim while the game's thread is
			// executing in that region lets it fetch a half-written prologue.
			// These snapshot the target's thread list, suspend all of them, and
			// resume exactly the handles that were suspended.
			// -----------------------------------------------------------------

			inline std::vector<HANDLE> & SuspendedGameThreads( )
			{
				static std::vector<HANDLE> Handles;
				return Handles;
			}

			inline void SuspendGameThreads( )
			{
				auto & Handles = SuspendedGameThreads( );

				// A repeated call must not suspend twice - a thread suspended
				// twice needs two resumes, and ResumeGameThreads only resumes
				// once per stored handle.
				if ( !Handles.empty( ) )
					return;

				const DWORD ProcessId = g_Variables.ProcIdFiveM;
				if ( !ProcessId )
					return;

				HANDLE Snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
				if ( Snapshot == INVALID_HANDLE_VALUE )
					return;

				THREADENTRY32 Entry = {};
				Entry.dwSize = sizeof( Entry );

				if ( Thread32First( Snapshot, &Entry ) )
				{
					do
					{
						if ( Entry.th32OwnerProcessID != ProcessId )
							continue;

						HANDLE Thread = OpenThread( THREAD_SUSPEND_RESUME, FALSE, Entry.th32ThreadID );
						if ( !Thread )
							continue;

						if ( SuspendThread( Thread ) != static_cast< DWORD >( -1 ) )
							Handles.push_back( Thread );
						else
							CloseHandle( Thread );

					} while ( Thread32Next( Snapshot, &Entry ) );
				}

				CloseHandle( Snapshot );
			}

			inline void ResumeGameThreads( )
			{
				auto & Handles = SuspendedGameThreads( );

				for ( HANDLE Thread : Handles )
				{
					ResumeThread( Thread );
					CloseHandle( Thread );
				}

				Handles.clear( );
			}

			// -----------------------------------------------------------------

			inline bool IsOnScreen( D3DXVECTOR2 Pos )
			{
				if ( Pos == D3DXVECTOR2( 0, 0 ) ) return false;
				return true;
			}

			// Visibility gate for the visible-only option: a ped counts as
			// visible when its head projects into the viewport. Declared after
			// IsOnScreen and before GetClosestPed, which calls it.
			inline bool IsEntityVisible( CPed * Ped )
			{
				if ( !Ped )
					return false;

				if ( !Core::SDK::Pointers::pViewPort )
					return false;

				const D3DXVECTOR3 HeadPos = Ped->GetBonePosDefault( 0 );
				return IsOnScreen( WorldToScreen( HeadPos ) );
			}

		}
	}

}
