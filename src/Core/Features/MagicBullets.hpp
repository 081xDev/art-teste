#pragma once
#include <Includes/Includes.hpp>
#include <Core/Offsets.hpp>
#include <Core/SDK/SDK.hpp>



namespace Core
{
	namespace Features
	{
		class cMagicBullets {
		private:
			bool Initialized = false;
			uintptr_t CWeapon = 0;
			uintptr_t GetCWeaponObj( );
		public:
			void Initialize( );
			void Restore( );
			void Start( );
		};

		inline cMagicBullets g_MagicBullets;

	}
}