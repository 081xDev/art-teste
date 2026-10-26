#pragma once
#include <Includes/Includes.hpp>
#include <Includes/Utils.hpp>
#include <Core/Offsets.hpp>
#include <Core/Core.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <regex>


namespace Core
{
    namespace Threads
    {
        class cVehicleList {
        private:
            CVehInterFace * VehInterface = nullptr;
            CVehicleList * VehList = nullptr;
            int MaxVehicles = 0;
        public:
            // No mutex here on purpose. The reader (Esp.cpp) locks
            // Core::SDK::Game::VehicleListMutex; a second, private mutex here
            // would mean writer and reader take different locks and the
            // protection is decorative - the reader could copy the vector while
            // the writer is move-assigning into it. One mutex, declared next to
            // the vector it guards, in SDK.hpp.
        public:
            void Update( )
            {
                while ( true )
                {

                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );

                   // if ( !g_MenuInfo.IsLogged && !g_Variables.g_bPassedByThisVerify )
                        //continue;

                    if ( !Core::SDK::Pointers::pReplayInterFace )
                        continue;

                    // The local player is needed for the distance term; without
                    // it every vehicle read below would dereference null.
                    CPed * LocalPlayer = Core::SDK::Pointers::pLocalPlayer;
                    if ( !LocalPlayer )
                        continue;

                    if ( !VehInterface || !VehList )
                    {
                        VehInterface = Core::SDK::Pointers::pReplayInterFace->InterfaceVeh( );
                        if ( !VehInterface )
                            continue; //*

                        VehList = VehInterface->VehicleList( );
                        if ( !VehList )
                            continue; //*
                    };

                    if ( !MaxVehicles )
                        MaxVehicles = VehInterface->MaxVehicles( );

                    // The old `size_t Size = ...VehicleList.size()` here read the
                    // shared vector from the writer thread with no lock and was
                    // never used.

                    std::vector<Core::SDK::Game::VehicleStructure> updatedVehicleList;

                    for ( int i = 0; i < MaxVehicles; i++ )
                    {
                        CVehicle * CurrentVeh = VehList->Vehicle( i );
                        if ( !CurrentVeh )
                            continue;

                        uintptr_t vehicleModelInfo = Mem.Read<uintptr_t>( ( uintptr_t ) CurrentVeh + 0x20 );
                        if ( !vehicleModelInfo )
                            continue;

                        std::string vehicleName = Mem.ReadString( vehicleModelInfo + 0x298 );
                        if ( vehicleName.empty( ) )
                            continue;

                        D3DXVECTOR3 DistanceCalculation = CurrentVeh->GetPos( ) - LocalPlayer->GetPos( );
                        float Distance = sqrtf( DistanceCalculation.x * DistanceCalculation.x + DistanceCalculation.y * DistanceCalculation.y + DistanceCalculation.z * DistanceCalculation.z );

                        Core::SDK::Game::VehicleStructure Vehicle;

                        Vehicle.Pointer = CurrentVeh;
                        Vehicle.Name = Utils::StringToFirstUpperCase( vehicleName );
                        Vehicle.Dist = Distance;
                        Vehicle.IsLocked = CurrentVeh->IsLocked( );
                        Vehicle.Driver = CurrentVeh->GetDriver( );
                        Vehicle.Pos = CurrentVeh->GetPos( );

                        updatedVehicleList.push_back( Vehicle );
                    }

                    std::sort( updatedVehicleList.begin( ), updatedVehicleList.end( ), [ ] ( const auto & lhs, const auto & rhs ) { return lhs.Dist < rhs.Dist; } );

                    {
                        std::lock_guard<std::mutex> lock( Core::SDK::Game::VehicleListMutex );
                        Core::SDK::Game::VehicleList = std::move( updatedVehicleList );
                    }

                }
            }
        };

        inline cVehicleList g_VehicleList;
    }
}