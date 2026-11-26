//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        
//
// Description: Implement MissionCondition
//
// History:     09/07/2002 + Created -- NAME
//
//=============================================================================

//========================================
// System Includes
//========================================
// Foundation Tech
#include <raddebug.hpp>

//========================================
// Project Includes
//========================================

#include <mission/conditions/missioncondition.h>

#include <events/eventmanager.h>
#include <meta/locator.h>
#include <meta/triggerlocator.h>
#include <meta/triggervolume.h>
#include <p3d/utility.hpp>
#include <worldsim/redbrick/vehicle.h>
#include <string.h>

//******************************************************************************
//
// Global Data, Local Data, Local Classes
//
//******************************************************************************

bool MissionCondition::mFailedHitNRun = false;

//******************************************************************************
//
// Public Member Functions
//
//******************************************************************************

//==============================================================================
// MissionCondition::MissionCondition
//==============================================================================
// Description: Constructor.
//
// Parameters: None.
//
// Return:      N/A.
//
//==============================================================================
MissionCondition::MissionCondition() :
    mType( COND_INVALID ),
    mbIsViolated( false ),
    mLeaveInterior( false )
{
}

//==============================================================================
// MissionCondition::~MissionCondition
//==============================================================================
// Description: Destructor.
//
// Parameters: None.
//
// Return:      N/A.
//
//==============================================================================
MissionCondition::~MissionCondition()
{
}

//=============================================================================
// MissionCondition::Initialize
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void MissionCondition::Initialize()
{
    mbIsViolated = false;
    mLeaveInterior = false;

    mFailedHitNRun = false;
    //GetEventManager()->AddListener( this, EVENT_HIT_AND_RUN_CAUGHT );

    OnInitialize();
}

//=============================================================================
// MissionCondition::Finalize
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      void 
//
//=============================================================================
void MissionCondition::Finalize()
{
    //GetEventManager()->RemoveListener( this, EVENT_HIT_AND_RUN_CAUGHT );

    OnFinalize();

    mFailedHitNRun = false;
}

//=============================================================================
// MissionCondition::HandleEvent
//=============================================================================
// Description: Comment
//
// Parameters:  ( EventEnum id, void* pEventData )
//
// Return:      void 
//
//=============================================================================
void MissionCondition::HandleEvent( EventEnum id, void* pEventData )
{
    //if ( id == EVENT_HIT_AND_RUN_CAUGHT )
    //{
    //    mbIsViolated = true;
    //    mFailedHitNRun = true;
    //}
}

//=============================================================================
// MissionCondition::IsChaseCondition
//=============================================================================
// Description: Indicates whether this condition involves chasing.  Assume
//              false, and let those classes which have chases override this
//              function
//
// Parameters:  None
//
// Return:      Default value of false, to be overridden by chase conditions 
//
//=============================================================================
bool MissionCondition::IsChaseCondition()
{
    return( false );
}

//=============================================================================
// MissionCondition::IsClose
//=============================================================================
// Description: Indicates whether this condition is at all close to failure.
//              Very subjective, used for interactive music
//
// Parameters:  None
//
// Return:      True if close, false otherwise 
//
//=============================================================================
bool MissionCondition::IsClose()
{
    return( true );
}

//=============================================================================
// InsideTriggerCondition
//=============================================================================
InsideTriggerCondition::InsideTriggerCondition() :
    mThreshold( 0.0f ),
    mDecayRate( 0.0f ),
    mMaxVal( 0.0f ),
    mTimer( 0.0f ),
    mLocator( NULL ),
    mWasActive( true ),
    mSavedActive( false )
{
    SetType( COND_INSIDE_TRIGGER );
    mTriggerName[ 0 ] = '\0';
    mSoundName[ 0 ] = '\0';
}

InsideTriggerCondition::~InsideTriggerCondition()
{
}

void InsideTriggerCondition::SetTriggerName( const char* name )
{
    if( name != NULL )
    {
        strncpy( mTriggerName, name, sizeof( mTriggerName ) - 1 );
        mTriggerName[ sizeof( mTriggerName ) - 1 ] = '\0';
    }
}

void InsideTriggerCondition::SetSound( const char* soundName, const char* /*triggerType*/ )
{
    if( soundName != NULL )
    {
        strncpy( mSoundName, soundName, sizeof( mSoundName ) - 1 );
        mSoundName[ sizeof( mSoundName ) - 1 ] = '\0';
    }
}

void InsideTriggerCondition::OnInitialize()
{
    mTimer = 0.0f;
    mSavedActive = false;
    mLocator = ( mTriggerName[ 0 ] != '\0' ) ? p3d::find<Locator>( mTriggerName ) : NULL;
    if( mLocator != NULL )
    {
        mWasActive = mLocator->GetFlag( Locator::ACTIVE );
        mSavedActive = true;
        mLocator->SetFlag( Locator::ACTIVE, true );
    }
}

void InsideTriggerCondition::OnFinalize()
{
    if( mLocator != NULL && mSavedActive )
    {
        mLocator->SetFlag( Locator::ACTIVE, mWasActive );
    }
    mLocator = NULL;
    mSavedActive = false;
}

void InsideTriggerCondition::Update( unsigned int elapsedTime )
{
    if( IsViolated() || mTriggerName[ 0 ] == '\0' )
    {
        return;
    }
    if( mLocator == NULL )
    {
        mLocator = p3d::find<Locator>( mTriggerName );
        if( mLocator != NULL && !mSavedActive )
        {
            mWasActive = mLocator->GetFlag( Locator::ACTIVE );
            mSavedActive = true;
            mLocator->SetFlag( Locator::ACTIVE, true );
        }
    }
    if( mLocator == NULL )
    {
        return;
    }

    bool inside = false;
    TriggerLocator* trigLoc = dynamic_cast<TriggerLocator*>( mLocator );
    if( trigLoc != NULL )
    {
        for( unsigned int i = 0; i < trigLoc->GetNumTriggers(); ++i )
        {
            if( trigLoc->GetTriggerVolume( i )->IsPlayerTracking( 0 ) )
            {
                inside = true;
                break;
            }
        }
    }

    float dtSec = static_cast<float>( elapsedTime ) * 0.001f;
    if( inside )
    {
        if( mThreshold <= 0.0f )
        {
            SetIsViolated( true );
        }
        else
        {
            mTimer += dtSec;
            if( mTimer >= mThreshold )
            {
                SetIsViolated( true );
            }
        }
    }
    else if( mDecayRate > 0.0f && mTimer > 0.0f )
    {
        mTimer -= mDecayRate * dtSec;
        if( mTimer < 0.0f )
        {
            mTimer = 0.0f;
        }
    }
}

bool InsideTriggerCondition::IsClose()
{
    return ( mTimer > 0.0f );
}

//=============================================================================
// DestroyCarsCondition
//=============================================================================
DestroyCarsCondition::DestroyCarsCondition() :
    mNumTargets( 0 )
{
    SetType( COND_DESTROY_CARS );
}

DestroyCarsCondition::~DestroyCarsCondition()
{
}

void DestroyCarsCondition::AddTargetModel( const char* model )
{
    if( model != NULL && model[ 0 ] != '\0' && mNumTargets < MAX_TARGETS )
    {
        strncpy( mTargetModels[ mNumTargets ], model, 63 );
        mTargetModels[ mNumTargets ][ 63 ] = '\0';
        mNumTargets++;
    }
}

void DestroyCarsCondition::OnInitialize()
{
    GetEventManager()->AddListener( this, EVENT_VEHICLE_DESTROYED );
}

void DestroyCarsCondition::OnFinalize()
{
    GetEventManager()->RemoveListener( this, EVENT_VEHICLE_DESTROYED );
}

void DestroyCarsCondition::HandleEvent( EventEnum id, void* pEventData )
{
    if( !IsViolated() && id == EVENT_VEHICLE_DESTROYED && pEventData != NULL )
    {
        Vehicle* v = static_cast<Vehicle*>( pEventData );
        const char* vName = v->GetName();
        if( mNumTargets == 0 )
        {
            SetIsViolated( true );
        }
        else if( vName != NULL )
        {
            for( int i = 0; i < mNumTargets; ++i )
            {
                if( strcmp( vName, mTargetModels[ i ] ) == 0 )
                {
                    SetIsViolated( true );
                    break;
                }
            }
        }
    }
    MissionCondition::HandleEvent( id, pEventData );
}

//******************************************************************************
//
// Private Member Functions
//
//******************************************************************************
