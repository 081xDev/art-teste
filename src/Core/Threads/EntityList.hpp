#pragma once
#include <Includes/Includes.hpp>
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>

namespace Core {
    namespace Threads {

        class cEntityList {
        private:
            std::mutex EntityListMutex;

        public:
            inline std::string getPlayerNameByNetId(int netid) {
                uintptr_t ModBase = 0;

                const uintptr_t playernames = Mem.GetModuleBaseAddr(g_Variables.ProcIdFiveM, "citizen-playernames-five.dll", &ModBase);
                if (!playernames)
                    return "npc";

                const uintptr_t PlayerNamesArray = Mem.Read<uintptr_t>(playernames + 0x30D38);
                if (!PlayerNamesArray)
                    return "npc";

                const int LastPlayer = Mem.Read<int>(playernames + 0x30D38 + 0x8);
                if (LastPlayer <= 0 || LastPlayer > 2048)
                    return "npc";

                uintptr_t list = Mem.Read<uintptr_t>(PlayerNamesArray + 0x8);

                for (int i = 0; i < LastPlayer; i++) {
                    if (!list)
                        break;

                    const int id = Mem.Read<int>(list + 0x10);
                    if (netid == id) {
                        const std::string name = Mem.ReadString(list + 0x18);
                        return name.empty() ? "npc" : name;
                    }

                    list = Mem.Read<uintptr_t>(list + 8);
                }

                return "npc";
            }

            void Update() {
                while (true) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));

                    if (!g_MenuInfo.IsLogged && !g_Variables.g_bPassedByThisVerify)
                        continue;

                    // Resolve the local player once per tick and bail if it is
                    // not there yet. The distance calculation dereferenced it
                    // unconditionally on a 1 ms loop.
                    CPed* LocalPlayer = Core::SDK::Pointers::pLocalPlayer;
                    if (!LocalPlayer)
                        continue;

                    if (!Core::SDK::Pointers::pReplayInterFace)
                        continue;

                    CPedInterFace* PedInterface = Core::SDK::Pointers::pReplayInterFace->InterfacePed();
                    if (!PedInterface)
                        continue;

                    CPedList* PedList = PedInterface->PedList();
                    if (!PedList)
                        continue;

                    const int PedCount = PedInterface->MaxPed();
                    if (PedCount <= 0 || PedCount > 4096)
                        continue;

                    const D3DXVECTOR3 LocalPos = LocalPlayer->GetPos();

                    // Build the frame off to the side, then publish it whole.
                    // The old code rewrote the live vector element by element
                    // with no lock, so the render thread could read a half
                    // updated list.
                    std::vector<Core::SDK::Game::EntityStruct> Snapshot;
                    Snapshot.reserve(static_cast<size_t>(PedCount));

                    for (int i = 0; i < PedCount; i++) {
                        CPed* CurrentPed = PedList->Ped(i);
                        if (!CurrentPed)
                            continue;

                        const float Health = CurrentPed->GetHealth();
                        if (Health <= 0.f)
                            continue;

                        const int PlayerId = CurrentPed->GetID();

                        Core::SDK::Game::EntityStruct Entity{};
                        Entity.Ped = CurrentPed;
                        Entity.Id = PlayerId;
                        Entity.Index = i;
                        Entity.Pos = CurrentPed->GetPos();

                        // find(), not operator[]. operator[] on an unordered_map
                        // default-constructs on a miss, so it MUTATES the
                        // container - and the render thread inserts into this
                        // same map under FriendMapMutex. Concurrent mutation is
                        // UB, and the misses also grew the map with a bogus
                        // entry for every ped ever seen.
                        {
                            std::lock_guard<std::mutex> FriendGuard( Core::SDK::Game::FriendMapMutex );
                            const auto FriendIt = Core::SDK::Game::FriendMap.find( CurrentPed );
                            if ( FriendIt != Core::SDK::Game::FriendMap.end( ) )
                                Entity.IsFriend = FriendIt->second;
                        }

                        Entity.MaxHealth = CurrentPed->GetMaxHealth();
                        Entity.Health = Health;
                        Entity.Armor = CurrentPed->GetArmor();
                        Entity.PedType = CurrentPed->GetPedType();
                        Entity.Distance = CurrentPed->GetDistance(LocalPos, Entity.Pos);

                        // The weapon name walks three pointers. Any of them can
                        // be null on a ped whose weapon manager is not up yet,
                        // which is what took the whole cheat down.
                        if (CWeaponManager* WeaponManager = CurrentPed->GetWeaponManager()) {
                            if (CWeaponInfo* WeaponInfo = WeaponManager->GetWeaponInfo())
                                Entity.WeaponName = WeaponInfo->GetName();
                        }

                        Entity.NetworkInfo.UserName = getPlayerNameByNetId(PlayerId);

                        // NetworkMap is written by the UpdateNames thread every
                        // few seconds. find() under a lock, for the same reason
                        // as FriendMap above.
                        {
                            std::lock_guard<std::mutex> NamesGuard( Core::Threads::g_UpdateNames.NetworkMapMutex );
                            const auto NameIt = Core::Threads::g_UpdateNames.NetworkMap.find( PlayerId );
                            if ( NameIt != Core::Threads::g_UpdateNames.NetworkMap.end( ) )
                            {
                                Entity.NetworkInfo.SteamId = NameIt->second.SteamId;
                                Entity.NetworkInfo.DiscordId = NameIt->second.DiscordId;
                            }
                        }

                        Snapshot.push_back(Entity);
                    }

                    {
                        std::lock_guard<std::mutex> Guard(Core::SDK::Game::EntityListMutex);
                        Core::SDK::Game::EntityList.swap(Snapshot);
                    }
                }
            }
        };

        inline cEntityList g_EntityList;
    }
}
