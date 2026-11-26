//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        missionobjective.h
//
// Description: Blahblahblah
//
// History:     15/04/2002 + Created -- NAME
//
//=============================================================================

#ifndef MISSIONOBJECTIVE_H
#define MISSIONOBJECTIVE_H

//========================================
// Nested Includes
//========================================

#include <radmath/radmath.hpp>
#include <events/eventlistener.h>
#include <presentation/gui/utility/hudmap.h>

#include <radmath/radmath.hpp>

#include <p3d/entity.hpp>

#include <roads/roadmanager.h>

#include <worldsim/character/charactercontroller.h>
#include <constants/directionalarrowenum.h>


//========================================
// Forward References
//========================================

class Vehicle;
class Locator;
class InstStatEntityDSG;
class AnimEntityDSG;
class CarStartLocator;
class Character;
class RoadSegment;

//=============================================================================
//
// Synopsis:    Blahblahblah
//
//=============================================================================

namespace MissionObjectiveNames
{
    const char* const Name[] = 
    {
        "dummy",
        "goto",
        "delivery",
        "follow",
        "destroy",
        "race",
        "losetail",
        "talkto",
        "dialogue",
        "getin",
        "dump",
        "fmv",
        "interior",
        "coins",
        "destroyboss",
        "loadvehicle",
        "pickupitem",
        "timer",
        "buycar",
        "buyskin",
        "gooutside",
        "insidetrigger",
        "outsidetrigger",
        "camera",
        "hitpeds",
        "destroycars"
    };

}

class MissionObjective : public EventListener
{
public:

    enum { MAX_NPC_WAYPOINT_LOCATORS = NPCController::MAX_NPC_WAYPOINTS };
    enum { MAX_NPCS = 4, MAX_NPC_NAME_LENGTH = 10 };

    MissionObjective();
    virtual ~MissionObjective();

    enum ObjectiveTypeEnum
    {
        OBJ_INVALID,
	    OBJ_GOTO,
	    OBJ_DELIVERY,
	    OBJ_FOLLOW,
	    OBJ_DESTROY,
        OBJ_RACE,
        OBJ_LOSETAIL,
        OBJ_TALKTO,
        OBJ_DIALOGUE,
        OBJ_GETIN,
        OBJ_DUMP,
        OBJ_FMV,
        OBJ_INTERIOR,
        OBJ_COIN,
        OBJ_DESTROY_BOSS,
        OBJ_LOAD_VEHICLE,
        OBJ_PICKUP_ITEM,
        OBJ_TIMER,
        OBJ_BUY_CAR,
        OBJ_BUY_SKIN,
        OBJ_GO_OUTSIDE,
        OBJ_INSIDE_TRIGGER,
        OBJ_OUTSIDE_TRIGGER,
        OBJ_CAMERA,
        OBJ_HIT_PEDS,
        OBJ_DESTROY_CARS,
	    NUM_OBJECTIVES
    };

    Vehicle* GetRequiredVehicle() { return( mVehicle ); }
    void SetRequiredVehicle( Vehicle* vehicle ) { mVehicle = vehicle; }

    void Initialize();
    void Finalize();

    bool IsFinished() { return ( mbFinished ); }
	void Update( unsigned int elapsedTime );

	virtual void HandleEvent( EventEnum id, void* pEventData );

    void AddNPC( const char* name, const char* location, bool driver);
    bool AddNPCWaypoint( const char* npc, const char* location );
    void RemoveNPC( const char* npc, bool driver);

    ObjectiveTypeEnum GetObjectiveType() const;
    void SetObjectiveType( const ObjectiveTypeEnum objType );

    void SetDirectionalArrowType( DirectionalArrowEnum::TYPE* ipArrowType ){ mArrowType = *ipArrowType; }
    bool IsPattyAndSelmaDialog() const;
    bool IsRaceDialog()          const;

    DirectionalArrowEnum::TYPE mArrowType;

    void SetRockOut(bool b) { mRockOut = b; }

#ifdef RAD_DEBUG
    rmt::Vector mPathStart, mPathEnd;
#endif

protected:
    struct PathStruct
    {
        PathStruct(){ mpTargetRoadSeg = NULL; }
        SwapArray<RoadManager::PathElement> mPathRoute;
        rmt::Vector  mTargetPosn;
        RoadSegment* mpTargetRoadSeg; 
        static const char msUpdateModulo = 60;
        char mUpdateCount;
    };

    void UpdateLightPath( PathStruct& orPathStruct );
    void UpdateGroundHeights( SwapArray<RoadManager::PathElement>& orPathRoute );
    void UpdateLongRoadArrows( PathStruct& orPathStruct );
    void UpdateAnimPosition( AnimEntityDSG* pAnimEntityDSG, rmt::Vector& irPosn );

    void LightCurrentPath( SwapArray<RoadManager::PathElement>& orPathRoute );
    void UnlightCurrentPath( SwapArray<RoadManager::PathElement>& orPathRoute );
    void UnlightPath( SwapArray<RoadManager::PathElement>& orPathRoute );
    void LightPath(   rmt::Vector& irDestinationPosn, 
                      PathStruct& orPathStruct );
    void LightPathRightWay( SwapArray<RoadManager::PathElement>& orPathRoute, int iStart, int iPastEnd );
    void LightPathWrongWay( SwapArray<RoadManager::PathElement>& orPathRoute, int iStart, int iPastEnd );
    void GetHeadingAlongPath( SwapArray<RoadManager::PathElement>& irPath, int i, 
                              RoadSegment* ipSourceRoadSeg, 
                              rmt::Vector& orPathHeading );

    int FindIntersectionInPath( SwapArray<RoadManager::PathElement>& irPath, Intersection*  ipIntToFind);
    int FindRoadInPath(         SwapArray<RoadManager::PathElement>& irPath, Road*          ipRoadToFind);

    bool FindNearestRoadIconAhead(  SwapArray<RoadManager::PathElement>& orPathRoute, 
                                    rmt::Vector&                         irPosn, 
                                    rmt::Vector&                         orIconPosn, 
                                    rmt::Vector&                         irIconFacing); 

    virtual void OnInitialize() {};
    virtual void OnFinalize() {};
    virtual void OnUpdate( unsigned int elapsedTime ) {};
    void SetFinished( bool bIsFinished ) { mbFinished = bIsFinished; };
    bool GetFinished() const { return mbFinished; };
    void RegisterLocator( Locator* locator, int& hudIndex, bool primary, HudMapIcon::eIconType icon );
    void RegisterPosition( const rmt::Vector& pos, int& hudIndex, bool primary, HudMapIcon::eIconType icon, IHudMapIconLocator* hml = NULL );
    void UnregisterLocator( int& hudIndex );
    void UnregisterPosition( int& hudIndex );
    int ChangeIconType( int hudIndex, HudMapIcon::eIconType type );

	InstStatEntityDSG* AddEntity( char* entityname, rmt::Vector pos );
	void DrawEntity( InstStatEntityDSG* entity );
	void HideEntity( InstStatEntityDSG* entity );


private:
    ObjectiveTypeEnum mObjType;
    bool mbFinished;
    Vehicle* mVehicle;

    typedef struct NPCStruct
    {
        NPCStruct() : npc( NULL ), numWayLocs( 0 ) {};
        Character* npc;
        char locator[32];
        tName wayLocs[ MAX_NPC_WAYPOINT_LOCATORS ];
        int numWayLocs;
        bool driver;
    } NPC;

    struct NPCRemove
    {
        char name[MAX_NPC_NAME_LENGTH ];
        bool driver;
    };

    char mNPCNames[MAX_NPCS][MAX_NPC_NAME_LENGTH];
    NPC mNPCs[ MAX_NPCS ];
    unsigned int mNumNPCs;

    unsigned int mNumRemoveNPCs;
    NPCRemove mRemoveNPCs[MAX_NPCS];

    bool mRockOut;
};

class TriggerObjective : public MissionObjective
{
public:
    TriggerObjective( bool insideMode );
    virtual ~TriggerObjective();

    void SetTriggerName( const char* name );

protected:
    virtual void OnInitialize();
    virtual void OnFinalize();
    virtual void OnUpdate( unsigned int elapsedTime );

private:
    bool mInsideMode;
    char mTriggerName[ 64 ];
    Locator* mLocator;
    bool mWasActive;
    bool mSavedActive;
};

class CameraObjective : public MissionObjective
{
public:
    CameraObjective();
    virtual ~CameraObjective();

    void SetCameraName( const char* name );
    void SetMulticontName( const char* name );
    void SetCanSkip( bool canSkip ) { mCanSkip = canSkip; }
    virtual void HandleEvent( EventEnum id, void* pEventData );

protected:
    virtual void OnInitialize();
    virtual void OnFinalize();
    virtual void OnUpdate( unsigned int elapsedTime );

private:
    char mCameraName[ 64 ];
    char mMulticontName[ 64 ];
    bool mCanSkip;
    bool mListening;
    unsigned int mElapsedMs;
};

class MultiTargetObjective : public MissionObjective
{
public:
    explicit MultiTargetObjective( bool destroyCarsMode );
    virtual ~MultiTargetObjective();

    void AddTargetModel( const char* model );
    void SetTotal( int total ) { mTotal = ( total > 0 ) ? total : 1; }
    virtual void HandleEvent( EventEnum id, void* pEventData );

protected:
    virtual void OnInitialize();
    virtual void OnFinalize();
    virtual void OnUpdate( unsigned int elapsedTime );

private:
    enum { MAX_TARGETS = 8 };
    bool mDestroyCarsMode;
    char mTargetModels[ MAX_TARGETS ][ 64 ];
    int mNumTargets;
    int mTotal;
    int mCount;
    unsigned int mElapsedMs;
    void* mLastHitPtr;
    unsigned int mLastHitTimeMs;
};

#endif //MISSIONOBJECTIVE_H
