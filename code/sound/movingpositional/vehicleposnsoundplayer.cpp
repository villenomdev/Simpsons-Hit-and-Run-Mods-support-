//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        vehiclepositionalsoundplayer.cpp
//
// Description: Implement VehiclePositionalSoundPlayer
//
// History:     3/7/2003 + Created -- Esan
//
//=============================================================================

//========================================
// System Includes
//========================================
#include <radnamespace.hpp>

//========================================
// Project Includes
//========================================
#include <sound/movingpositional/vehicleposnsoundplayer.h>

#include <sound/soundrenderer/soundrenderingmanager.h>
#include <sound/soundfx/positionalsoundsettings.h>

#include <worldsim/redbrick/vehicle.h>


//*****************************************************************************
//
// Global Data, Local Data, Local Classes
//
//*****************************************************************************

//*****************************************************************************
//
// Public Member Functions
//
//*****************************************************************************

//=============================================================================
// VehiclePositionalSoundPlayer::VehiclePositionalSoundPlayer
//=============================================================================
// Description: Constructor.
//
// Parameters:  None.
//
// Return:      N/A.
//
//=============================================================================
VehiclePositionalSoundPlayer::VehiclePositionalSoundPlayer( ) :
    m_vehicle( NULL ),
    m_tiePitchToVelocity( false )
{
}

//=============================================================================
// VehiclePositionalSoundPlayer::~VehiclePositionalSoundPlayer
//=============================================================================
// Description: Destructor.
//
// Parameters:  None.
//
// Return:      N/A.
//
//=============================================================================
VehiclePositionalSoundPlayer::~VehiclePositionalSoundPlayer()
{
}

//=============================================================================
// AIVehicleSoundPlayer::ActivateByName
//=============================================================================
// Description: Start playing given sound for given vehicle
//
// Parameters:  soundName - name of positional sound setting object containing
//                          data on sound to play
//              theCar - vehicle to associate sound with
//
// Return:      void 
//
//=============================================================================
void VehiclePositionalSoundPlayer::ActivateByName( const char* soundName, Vehicle* theCar )
{
    IRadNameSpace* nameSpace;
    IRefCount* nameSpaceObj;
    positionalSoundSettings* parameters;

    //
    // Find the tunable sound settings that go with the name
    //
    nameSpace = Sound::daSoundRenderingManagerGet()->GetTuningNamespace();
    rAssert( nameSpace != NULL );
    nameSpaceObj = nameSpace->GetInstance( ::radMakeKey32( soundName ) );
    if( nameSpaceObj != NULL )
    {
        parameters = reinterpret_cast<positionalSoundSettings*>( nameSpaceObj );

        Activate( parameters, parameters->GetClipName(), theCar );
    }
    else
    {
        rDebugString( "Couldn't play AI car sound, no matching settings found" );
    }
}

//=============================================================================
// AIVehicleSoundPlayer::Activate
//=============================================================================
// Description: Start playing given sound for given vehicle
//
// Parameters:  soundSettings - positional sound setting object containing
//                              data on sound to play
//              resourceName - name of sound resource to play positionally
//              theCar - vehicle to associate sound with
//
// Return:      void 
//
//=============================================================================
void VehiclePositionalSoundPlayer::Activate( positionalSoundSettings* soundSettings,
                                             const char* resourceName,
                                             Vehicle* theCar )
{
    rmt::Vector posn;

    m_vehicle = theCar;

    m_player.SetPositionCarrier( *this );
    m_player.SetParameters( soundSettings );

    theCar->GetPosition( &posn );
    m_player.SetPosition( posn.x, posn.y, posn.z );

    m_player.PlaySound( resourceName, NULL );
}

//=============================================================================
// VehiclePositionalSoundPlayer::Deactivate
//=============================================================================
// Description: Stop playing sound.
//
// Parameters:  None
//
// Return:      void 
//
//=============================================================================
void VehiclePositionalSoundPlayer::Deactivate()
{
    m_vehicle = NULL;
    m_tiePitchToVelocity = false;

    m_player.Stop();
    m_player.UnsetPositionCarrier();
}

//=============================================================================
// VehiclePositionalSoundPlayer::GetPosition
//=============================================================================
// Description: Return position of vehicle we're playing sound for
//
// Parameters:  position - vector to be filled in with position
//
// Return:      void 
//
//=============================================================================
void VehiclePositionalSoundPlayer::GetPosition( radSoundVector& position )
{
    if( m_vehicle != NULL )
    {
        rmt::Vector vehiclePosn;
        m_vehicle->GetPosition( &vehiclePosn );
        position.SetElements( vehiclePosn.x, vehiclePosn.y, vehiclePosn.z );
    }
    else
    {
        position.SetElements( 0.0f, 0.0f, 0.0f );
    }
}

//=============================================================================
// VehiclePositionalSoundPlayer::GetVelocity
//=============================================================================
// Description: Return velocity of vehicle we're playing sound for
//
// Parameters:  velocity - vector to be filled in with velocity
//
// Return:      void 
//
//=============================================================================
void VehiclePositionalSoundPlayer::GetVelocity( radSoundVector& velocity )
{
    if( m_vehicle != NULL )
    {
        rmt::Vector vehicleVel;
        m_vehicle->GetVelocity( &vehicleVel );
        velocity.SetElements( vehicleVel.x, vehicleVel.y, vehicleVel.z );
    }
    else
    {
        velocity.SetElements( 0.0f, 0.0f, 0.0f );
    }
}

//=============================================================================
// VehiclePositionalSoundPlayer::ServiceOncePerFrame
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void VehiclePositionalSoundPlayer::ServiceOncePerFrame()
{
    m_player.ServiceOncePerFrame();
}

//=============================================================================
// VehiclePositionalSoundPlayer::UsesVehicle
//=============================================================================
// Description: Indicate whether we're currently playing sound for given
//              vehicle
//
// Parameters:  car - vehicle to match against
//
// Return:      true if we're playing sound for that car, false otherwise 
//
//=============================================================================
bool VehiclePositionalSoundPlayer::UsesVehicle( Vehicle* car )
{
    return( ( m_vehicle != NULL ) && ( car == m_vehicle ) );
}

//=============================================================================
// VehiclePositionalSoundPlayer::BlowUp
//=============================================================================
// Description: Play an explosion sound.  Yay!
//
// Parameters:  None
//
// Return:      void 
//
//=============================================================================
void VehiclePositionalSoundPlayer::BlowUp()
{
    if( m_vehicle == NULL )
    {
        return;
    }

    IRadNameSpace* nameSpace;
    positionalSoundSettings* settings;
    rmt::Vector position;

    nameSpace = Sound::daSoundRenderingManagerGet()->GetTuningNamespace();
    rAssert( nameSpace != NULL );
    settings = reinterpret_cast<positionalSoundSettings*>( nameSpace->GetInstance( "collision_sounds" ) );
    if( settings != NULL )
    {
        m_vehicle->GetPosition( &position );
        m_explosionPlayer.SetPosition( position.x, position.y, position.z );
        m_explosionPlayer.SetPositionCarrier( *this );
        m_explosionPlayer.SetParameters( settings );
        m_explosionPlayer.PlaySound( "generic_car_explode" );
    }
    else
    {
        rDebugString( "Couldn't find positional explosion settings for AI vehicle" );
    }
}

//*****************************************************************************
//
// Private Member Functions
//
//*****************************************************************************
