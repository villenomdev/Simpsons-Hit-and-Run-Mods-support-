//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        missioncondition.h
//
// Description: Blahblahblah
//
// History:     09/07/2002 + Created -- NAME
//
//=============================================================================

#ifndef MISSIONCONDITION_H
#define MISSIONCONDITION_H

//========================================
// Nested Includes
//========================================

#include <events/eventlistener.h>

//========================================
// Forward References
//========================================

//=============================================================================
//
// Synopsis:    Blahblahblah
//
//=============================================================================

namespace MissionConditionNames
{
    const char* const Name[] = 
    {
        "invalid",
        "damage",
        "playerhit",
        "timeout",
        "outofvehicle",
        "followdistance",
        "outofbounds",
        "race",
        "leaveinterior",
        "position",
        "carryingspcollectible",
        "notabducted",
        "hitandruncought",
        "keepbarrel",
        "getcollectibles",
        "insidetrigger",
        "destroycars"
    };

}

class MissionCondition : public EventListener
{
public:
    MissionCondition();
    virtual ~MissionCondition();

    enum ConditionTypeEnum
    {
        COND_INVALID,
	    COND_VEHICLE_DAMAGE,
	    COND_PLAYER_HIT,
	    COND_TIME_OUT,
	    COND_PLAYER_OUT_OF_VEHICLE,
        COND_FOLLOW_DISTANCE,
	    COND_OUT_OF_BOUNDS,
        COND_RACE,
        COND_LEAVE_INTERIOR,
        COND_POSITION,
        COND_CARRYING_STATEPROP_COLLECTIBLE,
        COND_NOT_ABDUCTED,
        COND_HIT_AND_RUN_CAUGHT,
        COND_KEEP_BARREL,
        COND_GET_COLLECTIBLES,
        COND_INSIDE_TRIGGER,
        COND_DESTROY_CARS,
	    NUM_CONDITIONS
    };

	ConditionTypeEnum GetType() const;

    // Index in the text bible of the description for this condition
	//unsigned int GetDescriptionIndex();
    // Index in the text bible of the message when this condition fails
	//unsigned int GetFailMessageIndex();

    void Initialize();
    void Finalize();

    // Returns true if this condition has been violated
    bool IsViolated();
    bool LeaveInterior();

    // the Mission Stage should call this every frame
    virtual void Update( unsigned int elapsedTime ) {};

    virtual void HandleEvent( EventEnum id, void* pEventData );

    virtual bool IsChaseCondition();
    virtual bool IsClose();
    void SetIsViolated( bool bIsViolated );

protected:
    virtual void OnInitialize() {};
    virtual void OnFinalize() {};

   	void SetType( ConditionTypeEnum type );
    
    void SetLeaveInterior( bool bIsLeaving );

    ConditionTypeEnum mType;

private:
    //Prevent wasteful constructor creation.
    MissionCondition( const MissionCondition& missioncondition );
    MissionCondition& operator=( const MissionCondition& missioncondition );

    bool mbIsViolated;
    bool mLeaveInterior;

    static bool mFailedHitNRun;  //Shared among all the conditions.
};


//=============================================================================
// MissionCondition::GetType
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      inline 
//
//=============================================================================
inline MissionCondition::ConditionTypeEnum
MissionCondition::GetType() const
{
    if ( mFailedHitNRun )
    {
        return COND_HIT_AND_RUN_CAUGHT;
    }

    return mType;
}

//=============================================================================
// MissionCondition::GetType
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      inline 
//
//=============================================================================
inline void
MissionCondition::SetType( MissionCondition::ConditionTypeEnum type )
{
    mType = type;
}

//=============================================================================
// MissionCondition::IsViolated
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      inline 
//
//=============================================================================
inline bool MissionCondition::IsViolated()
{
    return mbIsViolated;
}

//=============================================================================
// MissionCondition::LeaveInterior
//=============================================================================
// Description: Comment
//
// Parameters:  ()
//
// Return:      bool 
//
//=============================================================================
inline bool MissionCondition::LeaveInterior()
{
    return mLeaveInterior;
}

//=============================================================================
// MissionCondition::SetIsViolated
//=============================================================================
// Description: Comment
//
// Parameters:  ( bool bIsViolated )
//
// Return:      inline 
//
//=============================================================================
inline void MissionCondition::SetIsViolated( bool bIsViolated )
{
    mbIsViolated = bIsViolated;
}

//=============================================================================
// MissionCondition::SetLeaveInterior
//=============================================================================
// Description: Comment
//
// Parameters:  ( bool bIsLeaving )
//
// Return:      void 
//
//=============================================================================
inline void MissionCondition::SetLeaveInterior( bool bIsLeaving )
{
    mLeaveInterior = bIsLeaving;
}

class Locator;

class InsideTriggerCondition : public MissionCondition
{
public:
    InsideTriggerCondition();
    virtual ~InsideTriggerCondition();

    void SetTriggerName( const char* name );
    void SetThreshold( float seconds ) { mThreshold = seconds; }
    void SetDecay( float rate, float maxVal ) { mDecayRate = rate; mMaxVal = maxVal; }
    void SetSound( const char* soundName, const char* triggerType );

    virtual void Update( unsigned int elapsedTime );
    virtual bool IsClose();

protected:
    virtual void OnInitialize();
    virtual void OnFinalize();

private:
    char mTriggerName[ 64 ];
    char mSoundName[ 64 ];
    float mThreshold;
    float mDecayRate;
    float mMaxVal;
    float mTimer;
    Locator* mLocator;
    bool mWasActive;
    bool mSavedActive;
};

class DestroyCarsCondition : public MissionCondition
{
public:
    DestroyCarsCondition();
    virtual ~DestroyCarsCondition();

    void AddTargetModel( const char* model );
    virtual void HandleEvent( EventEnum id, void* pEventData );

protected:
    virtual void OnInitialize();
    virtual void OnFinalize();

private:
    enum { MAX_TARGETS = 8 };
    char mTargetModels[ MAX_TARGETS ][ 64 ];
    int mNumTargets;
};
 
#endif //MISSIONCONDITION_H
