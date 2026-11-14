//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenOptions
//
// Description: Implementation of the CGuiScreenOptions class.
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2002/07/04      TChu        Created for SRR2
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <presentation/gui/frontend/guiscreenoptions.h>
#include <presentation/gui/frontend/guiscreenplaymovie.h>
#include <presentation/gui/guimenu.h>
#include <presentation/gui/guisystem.h>
#include <presentation/gui/guimanager.h>
#include <presentation/gui/guiscreenmessage.h>
#include <presentation/gui/guiscreenprompt.h>
#include <render/RenderManager/RenderManager.h>
#include <radtime.hpp>

#include <cheats/cheatinputsystem.h>
#include <cheats/cheats.h>
#include <debug/gamelog.h>
#include <data/config/androidconfigurationmanager.h>
#include <mod_manager.hpp>
#include <events/eventmanager.h>
#include <memory/srrmemory.h>

#include <raddebug.hpp> // Foundation
#include <Layer.h>
#include <Page.h>
#include <Screen.h>
#include <Group.h>
#include <Text.h>

#ifdef RAD_ANDROID
#include <input/touch/touchhudsystem.h>
#endif

#ifdef RAD_PS2
    #include <main/ps2platform.h>
#endif

#ifdef RAD_PC
#include <input/FEMouse.h>
#endif

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

const unsigned int NUM_CHEAT_DISPLAY_LINES = 16;
const int PROGRESSIVE_MODE_TEST_TIME = 5000; // in msec

//===========================================================================
// Public Member Functions
//===========================================================================

//===========================================================================
// CGuiScreenOptions::CGuiScreenOptions
//===========================================================================
// Description: Constructor.
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
CGuiScreenOptions::CGuiScreenOptions
(
	Scrooby::Screen* pScreen,
	CGuiEntity* pParent
)
:   CGuiScreen( pScreen, pParent, GUI_SCREEN_ID_OPTIONS ),
    m_pMenu( NULL ),
    m_cheatsPage( NULL ),
    m_cheatsOverlay( NULL ),
    m_startingCheatID( 0 ),
    m_elapsedProgressiveModeTestTime( -1 ),
    m_inModsMenu( false ),
    m_modsMenuSelection( 0 ),
    m_modsMenuTopIndex( 0 ),
    m_inModLoadingState( false ),
    m_inModErrorState( false ),
    m_lastProgressRenderTime( 0 )
{
MEMTRACK_PUSH_GROUP( "CGUIScreenOptions" );
    // Retrieve the Scrooby drawing elements.
    //
    Scrooby::Page* pPage = m_pScroobyScreen->GetPage( "Options" );
    rAssert( pPage != NULL );

    // Create a menu.
    //
    m_pMenu = new CGuiMenu( this, NUM_MENU_ITEMS );
    rAssert( m_pMenu != NULL );

    // Add menu items
    //
    Scrooby::Group* pGroup = pPage->GetGroup( "Menu" );
    rAssert( pGroup != NULL );

#ifdef RAD_PC
    m_pMenu->AddMenuItem( pGroup->GetText( "Display" ) );
#endif
    m_pMenu->AddMenuItem( pGroup->GetText( "Controller" ) );
    m_pMenu->AddMenuItem( pGroup->GetText( "Sound" ) );
    m_pMenu->AddMenuItem( pGroup->GetText( "ViewMovies" ) );
    m_pMenu->AddMenuItem( pGroup->GetText( "ViewCredits" ) );



    // Setup Gamelog menu item, placed directly below View Credits
    Scrooby::Text* gamelogText = pGroup->GetText( "DisplayMode" );
    if( gamelogText != NULL )
    {
        Scrooby::Text* creditsText = pGroup->GetText( "ViewCredits" );
        Scrooby::Text* moviesText = pGroup->GetText( "ViewMovies" );
        if( creditsText != NULL )
        {
            int cWidth = 0, cHeight = 0;
            creditsText->GetBoundingBoxSize( cWidth, cHeight );
            int cCenterX = 0, cCenterY = 0;
            creditsText->GetBoundingBoxCenter( cCenterX, cCenterY );

            int spacingY = -38;
            if( moviesText != NULL )
            {
                int mCenterX = 0, mCenterY = 0;
                moviesText->GetBoundingBoxCenter( mCenterX, mCenterY );
                int diff = cCenterY - mCenterY;
                if( diff < -15 && diff > -75 )
                {
                    spacingY = diff;
                }
            }

            int targetWidth = ( cWidth > 320 ) ? cWidth : 320;
            int targetHeight = ( cHeight > 32 ) ? cHeight : 32;

            gamelogText->SetBoundingBoxSize( targetWidth, targetHeight );
            gamelogText->SetHorizontalJustification( creditsText->GetHorizontalJustification() );
            gamelogText->SetVerticalJustification( creditsText->GetVerticalJustification() );
            gamelogText->SetPositionOfCenter( cCenterX, cCenterY + spacingY );
            gamelogText->SetColour( creditsText->GetColour() );
            gamelogText->SetAlpha( 1.0f );
        }

        gamelogText->SetString( 0, GameLog::IsEnabled() ? "Gamelog (On)" : "Gamelog (Off)" );
        gamelogText->SetVisible( true );
    }

    // Hide unused DisplayMode elements
    Scrooby::Text* displayModeValue = pGroup->GetText( "DisplayMode_Value" );
    if( displayModeValue != NULL )
    {
        displayModeValue->SetVisible( false );
    }
    Scrooby::Sprite* arrowL = pGroup->GetSprite( "DisplayMode_ArrowL" );
    if( arrowL != NULL )
    {
        arrowL->SetVisible( false );
    }
    Scrooby::Sprite* arrowR = pGroup->GetSprite( "DisplayMode_ArrowR" );
    if( arrowR != NULL )
    {
        arrowR->SetVisible( false );
    }

    m_pMenu->AddMenuItem( gamelogText );

    // Setup Mods menu item, placed directly below Gamelog
    Scrooby::Text* modsText = pGroup->GetText( "Display" );
    if( modsText != NULL )
    {
        Scrooby::Text* creditsText = pGroup->GetText( "ViewCredits" );
        Scrooby::Text* moviesText = pGroup->GetText( "ViewMovies" );
        if( creditsText != NULL )
        {
            int cWidth = 0, cHeight = 0;
            creditsText->GetBoundingBoxSize( cWidth, cHeight );
            int cCenterX = 0, cCenterY = 0;
            creditsText->GetBoundingBoxCenter( cCenterX, cCenterY );

            int spacingY = -38;
            if( moviesText != NULL )
            {
                int mCenterX = 0, mCenterY = 0;
                moviesText->GetBoundingBoxCenter( mCenterX, mCenterY );
                int diff = cCenterY - mCenterY;
                if( diff < -15 && diff > -75 )
                {
                    spacingY = diff;
                }
            }

            int targetWidth = ( cWidth > 320 ) ? cWidth : 320;
            int targetHeight = ( cHeight > 32 ) ? cHeight : 32;

            modsText->SetBoundingBoxSize( targetWidth, targetHeight );
            modsText->SetHorizontalJustification( creditsText->GetHorizontalJustification() );
            modsText->SetVerticalJustification( creditsText->GetVerticalJustification() );
            modsText->SetPositionOfCenter( cCenterX, cCenterY + 2 * spacingY );
            modsText->SetColour( creditsText->GetColour() );
            modsText->SetAlpha( 1.0f );
        }

        char modsStr[64];
        ModManager::GetInstance().GetOptionsDisplayString( modsStr, sizeof(modsStr) );
        modsText->SetString( 0, modsStr );
        modsText->SetVisible( true );
    }

    m_pMenu->AddMenuItem( modsText );

#ifdef RAD_DEMO
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_VIEW_MOVIES, false );
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_VIEW_CREDITS, false );
#endif

#ifndef RAD_PC
    // Center the 6 visible menu items (Controller, Sound, ViewMovies, ViewCredits, Gamelog, Mods)
    pGroup->ResetTransformation();
    pGroup->Translate( 0, 36 );
#endif

    // get cheats page, if included
    //
    m_cheatsPage = m_pScroobyScreen->GetPage( "Cheats" );
    if( m_cheatsPage != NULL )
    {
        m_cheatsOverlay = m_cheatsPage->GetLayerByIndex( 0 );
    }

    // register self as cheat callback
    //
    GetCheatInputSystem()->RegisterCallback( this );
MEMTRACK_POP_GROUP( "CGUIScreenOptions" );
}


//===========================================================================
// CGuiScreenOptions::~CGuiScreenOptions
//===========================================================================
// Description: Destructor.
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
CGuiScreenOptions::~CGuiScreenOptions()
{
    #ifdef RAD_ANDROID
    TouchHudSystem::GetInstance().SetTouchControlsEditorMainMenuEntryAllowed( false );
    #endif 
    if( m_pMenu != NULL )
    {
        delete m_pMenu;
        m_pMenu = NULL;
    }

    // unregister self as cheat callback
    //
    GetCheatInputSystem()->UnregisterCallback( this );
}

#ifdef RAD_PC
//===========================================================================
// CGuiScreenOptions::CheckCursorAgainstHotspots
//===========================================================================
eFEHotspotType CGuiScreenOptions::CheckCursorAgainstHotspots( float x, float y )
{
    if( m_inModsMenu )
    {
        if( m_inModLoadingState )
        {
            return HOTSPOT_NONE;
        }

        if( m_inModErrorState )
        {
            return HOTSPOT_BUTTON;
        }

        if( m_cheatsPage != NULL )
        {
            const int MAX_DISPLAYED_MODS = 14;
            int modCount = ModManager::GetInstance().GetModCount();

            for( int i = 0; i < MAX_DISPLAYED_MODS; i++ )
            {
                int modIndex = m_modsMenuTopIndex + i;
                if( modIndex >= modCount ) break;

                char name[32];
                sprintf( name, "Cheat%d", i + 1 );
                Scrooby::Text* pText = m_cheatsPage->GetText( name );
                if( pText != NULL && pText->IsVisible() )
                {
                    if( pText->IsPointInBoundingRect( x, y ) )
                    {
                        if( m_modsMenuSelection != modIndex )
                        {
                            m_modsMenuSelection = modIndex;
                            this->UpdateModsMenuDisplay();
                        }
                        return HOTSPOT_BUTTON;
                    }
                }
            }

            // Also check footer text (Cheat15) -> BACK / Exit
            Scrooby::Text* footerText = m_cheatsPage->GetText( "Cheat15" );
            if( footerText != NULL && footerText->IsVisible() && footerText->IsPointInBoundingRect( x, y ) )
            {
                return HOTSPOT_BUTTON;
            }
        }

        return HOTSPOT_NONE;
    }

    return CGuiScreen::CheckCursorAgainstHotspots( x, y );
}
#endif


//===========================================================================
// CGuiScreenOptions::HandleMessage
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenOptions::HandleMessage
(
	eGuiMessage message, 
	unsigned int param1,
	unsigned int param2 
)
{
    if( GetCheatInputSystem()->IsCheatEnabled( CHEAT_ID_MOTHER_OF_ALL_CHEATS ) )
    {
        if( m_cheatsOverlay != NULL )
        {
            // show/hide cheats overlay, depending on whether or not
            // cheat input is activated on any of the controllers
            //
            bool isCheatInputActivated = false;
            int numUserInputHandlers = GetGuiSystem()->GetNumUserInputHandlers();
            for( int i = 0; i < numUserInputHandlers; i++ )
            {
                if( GetGuiSystem()->GetUserInputHandler( i ) != NULL &&
                    GetCheatInputSystem()->IsActivated( i ) )
                {
                    isCheatInputActivated = true;

                    break;
                }
            }

            m_cheatsOverlay->SetVisible( isCheatInputActivated );
        }

        if( message == GUI_MSG_CONTROLLER_RIGHT &&
            GetCheatInputSystem()->IsActivated( param1 ) )
        {
            // toggle cheat list
            //
            m_startingCheatID += NUM_CHEAT_DISPLAY_LINES;
            if( m_startingCheatID >= NUM_CHEATS )
            {
                // wrap to the beginning
                m_startingCheatID = 0;
            }

            this->UpdateCheatsDisplay( m_startingCheatID );
        }
    }

    if( this->IsControllerMessage( message ) &&
        GetCheatInputSystem()->IsActivated( param1 ) )
    {
        // ignore all controller inputs when cheat input system is activated
        return;
    }

    if( message == GUI_MSG_MENU_PROMPT_RESPONSE )
    {
        if( param1 == PROMPT_DISPLAY_PROGRESSIVE_SCAN_PS2 )
        {
            if( param2 == CGuiMenuPrompt::RESPONSE_YES )
            {
                // begin progressive scan test
                //
                this->ProgressiveModeTestBegin();
            }
            else
            {
                // don't switch display mode and return to options screen
                //
                this->ReloadScreen();
            }
        }
        else if( param1 == PROMPT_DISPLAY_PROGRESSIVE_SCAN_CONFIRM )
        {
            if( param2 == CGuiMenuPrompt::RESPONSE_YES )
            {
#ifdef RAD_PS2
                PS2Platform::GetInstance()->SetProgressiveMode( true );
#endif
                rTunePrintf( "Current display mode confirmed: PROGRESSIVE.\n" );
            }
            else
            {
                rTunePrintf( "Current display mode confirmed: INTERLACED.\n" );
            }

            // return to options screen
            //
            this->ReloadScreen();
        }
		else
			rTuneAssert(!"not handled");
    }
    else if( message == GUI_MSG_MESSAGE_UPDATE || message == GUI_MSG_PROMPT_UPDATE ) // update from message/prompt screen
    {
        if( m_elapsedProgressiveModeTestTime != -1 )
        {
            m_elapsedProgressiveModeTestTime += static_cast<int>( param1 );

            if( m_elapsedProgressiveModeTestTime > PROGRESSIVE_MODE_TEST_TIME )
            {
                this->OnProgressiveModeTestEnd();
            }
        }
    }

    if( m_state == GUI_WINDOW_STATE_RUNNING )
    {
        if( m_inModsMenu )
        {
            if( m_inModLoadingState )
            {
                return;
            }

            if( m_inModErrorState )
            {
                if( message == GUI_MSG_CONTROLLER_SELECT ||
                    message == GUI_MSG_CONTROLLER_START ||
                    message == GUI_MSG_CONTROLLER_BACK ||
                    message == GUI_MSG_MENU_SELECTION_MADE )
                {
                    m_inModErrorState = false;
                    this->UpdateModsMenuDisplay();
                    GetEventManager()->TriggerEvent( EVENT_FE_MENU_BACK );
                }
                return;
            }

            switch( message )
            {
                case GUI_MSG_CONTROLLER_UP:
                {
                    int count = ModManager::GetInstance().GetModCount();
                    if( count > 0 )
                    {
                        m_modsMenuSelection--;
                        if( m_modsMenuSelection < 0 )
                        {
                            m_modsMenuSelection = count - 1;
                        }
                        this->UpdateModsMenuDisplay();
                        GetEventManager()->TriggerEvent( EVENT_FE_MENU_UPORDOWN );
                    }
                    return;
                }
                case GUI_MSG_CONTROLLER_DOWN:
                {
                    int count = ModManager::GetInstance().GetModCount();
                    if( count > 0 )
                    {
                        m_modsMenuSelection++;
                        if( m_modsMenuSelection >= count )
                        {
                            m_modsMenuSelection = 0;
                        }
                        this->UpdateModsMenuDisplay();
                        GetEventManager()->TriggerEvent( EVENT_FE_MENU_UPORDOWN );
                    }
                    return;
                }
                case GUI_MSG_CONTROLLER_LEFT:
                {
                    int count = ModManager::GetInstance().GetModCount();
                    if( count > 0 )
                    {
                        m_modsMenuSelection -= 5;
                        if( m_modsMenuSelection < 0 )
                        {
                            m_modsMenuSelection = 0;
                        }
                        this->UpdateModsMenuDisplay();
                        GetEventManager()->TriggerEvent( EVENT_FE_MENU_UPORDOWN );
                    }
                    return;
                }
                case GUI_MSG_CONTROLLER_RIGHT:
                {
                    int count = ModManager::GetInstance().GetModCount();
                    if( count > 0 )
                    {
                        m_modsMenuSelection += 5;
                        if( m_modsMenuSelection >= count )
                        {
                            m_modsMenuSelection = count - 1;
                        }
                        this->UpdateModsMenuDisplay();
                        GetEventManager()->TriggerEvent( EVENT_FE_MENU_UPORDOWN );
                    }
                    return;
                }
                case GUI_MSG_CONTROLLER_SELECT:
                case GUI_MSG_CONTROLLER_START:
                case GUI_MSG_MENU_SELECTION_MADE:
                {
                    this->ActivateSelectedMod();
                    return;
                }
                case GUI_MSG_CONTROLLER_BACK:
                {
                    this->ExitModsMenu();
                    GetEventManager()->TriggerEvent( EVENT_FE_MENU_BACK );
                    return;
                }
                default:
                {
                    break;
                }
            }
            return;
        }

        switch( message )
        {
            case GUI_MSG_MENU_SELECTION_CHANGED:
            {
                this->SetButtonVisible( BUTTON_ICON_ACCEPT, true );

                break;
            }
            case GUI_MSG_CONTROLLER_LEFT:
            case GUI_MSG_CONTROLLER_RIGHT:
            {
                if( m_pMenu != NULL )
                {
                    if( m_pMenu->GetSelection() == MENU_ITEM_GAMELOG )
                    {
                        GameLog::Toggle();
                        GetAndroidConfigurationManager()->SetGamelogEnabled( GameLog::IsEnabled() );
                        GetAndroidConfigurationManager()->Save();
                        this->UpdateGamelogDisplay();
                        GetEventManager()->TriggerEvent( EVENT_FE_MENU_SELECT );
                    }
                    else if( m_pMenu->GetSelection() == MENU_ITEM_MODS )
                    {
                        int dir = ( message == GUI_MSG_CONTROLLER_RIGHT ) ? 1 : -1;
                        ModManager::GetInstance().CycleMod( dir );
                        this->UpdateModsDisplay();
                        GetEventManager()->TriggerEvent( EVENT_FE_MENU_SELECT );
                    }
                }

                break;
            }
            case GUI_MSG_MENU_SELECTION_MADE:
            {
                switch( param1 )
                {
                    case MENU_ITEM_CONTROLLER:
                    {
                        m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_CONTROLLER );

                        this->StartTransitionAnimation( 530, 560 );

                        break;
                    }
                    case MENU_ITEM_SOUND:
                    {
                        m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_SOUND );

                        this->StartTransitionAnimation( 660, 690 );

                        break;
                    }
                    case MENU_ITEM_VIEW_MOVIES:
                    {
                        m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_VIEW_MOVIES );

                        this->StartTransitionAnimation( 914, 944 );

                        break;
                    }
                    case MENU_ITEM_VIEW_CREDITS:
                    {
/*
                        rAssert( m_guiManager );
                        CGuiScreenPlayMovie* playMovieScreen = static_cast<CGuiScreenPlayMovie*>( m_guiManager->FindWindowByID( GUI_SCREEN_ID_VIEW_CREDITS ) );
                        rAssert( playMovieScreen );
                        playMovieScreen->SetMovieToPlay( MovieNames::CREDITS );
*/
                        m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_VIEW_CREDITS );

                        this->StartTransitionAnimation( 600, 630 );

                        break;
                    }
                    case MENU_ITEM_GAMELOG:
                    {
                        GameLog::Toggle();
                        GetAndroidConfigurationManager()->SetGamelogEnabled( GameLog::IsEnabled() );
                        GetAndroidConfigurationManager()->Save();
                        this->UpdateGamelogDisplay();
                        GetEventManager()->TriggerEvent( EVENT_FE_MENU_SELECT );

                        break;
                    }
                    case MENU_ITEM_MODS:
                    {
                        this->EnterModsMenu();
                        GetEventManager()->TriggerEvent( EVENT_FE_MENU_SELECT );

                        break;
                    }
#ifdef RAD_PC
                    case MENU_ITEM_DISPLAY:
                    {
                        m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_DISPLAY );

                        break;
                    }
#endif
                    default:
                    {
                        break;
                    }
                }

                break;
            }

            case GUI_MSG_CONTROLLER_BACK:
            {
                this->StartTransitionAnimation( 110, 140 );

                break;
            }

            default:
            {
                break;
            }
        }

        // relay message to menu
        if( m_pMenu != NULL )
        {
            m_pMenu->HandleMessage( message, param1, param2 );
        }
    }

	// Propogate the message up the hierarchy.
	//
	CGuiScreen::HandleMessage( message, param1, param2 );
}


//===========================================================================
// CGuiScreenOptions::OnCheatEntered
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void
CGuiScreenOptions::OnCheatEntered( eCheatID cheatID, bool isEnabled )
{
    this->UpdateCheatsDisplay( m_startingCheatID );

    if( cheatID == CHEAT_ID_MOTHER_OF_ALL_CHEATS && !isEnabled )
    {
        if( m_cheatsOverlay != NULL )
        {
            m_cheatsOverlay->SetVisible( false );
        }
    }
}


//===========================================================================
// CGuiScreenOptions::InitIntro
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenOptions::InitIntro()
{
#ifndef RAD_E3
    GetCheatInputSystem()->SetEnabled( true );
#endif

    #ifdef RAD_ANDROID
    TouchHudSystem::GetInstance().SetTouchControlsEditorMainMenuEntryAllowed( true );
    #endif

    rAssert( m_pMenu != NULL );
    this->SetButtonVisible( BUTTON_ICON_ACCEPT, true );
    this->UpdateGamelogDisplay();
    this->UpdateModsDisplay();
}


//===========================================================================
// CGuiScreenOptions::InitRunning
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenOptions::InitRunning()
{
    this->UpdateGamelogDisplay();
    this->UpdateModsDisplay();
}


//===========================================================================
// CGuiScreenOptions::InitOutro
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenOptions::InitOutro()
{
    if( m_inModsMenu )
    {
        this->ExitModsMenu();
    }

#ifndef RAD_E3
    GetCheatInputSystem()->SetEnabled( false );
#endif

#ifdef RAD_ANDROID
TouchHudSystem::GetInstance().SetTouchControlsEditorMainMenuEntryAllowed( false );
#endif
}


//---------------------------------------------------------------------
// Private Functions
//---------------------------------------------------------------------

void
CGuiScreenOptions::UpdateGamelogDisplay()
{
    Scrooby::Page* pPage = m_pScroobyScreen->GetPage( "Options" );
    if( pPage != NULL )
    {
        Scrooby::Group* pGroup = pPage->GetGroup( "Menu" );
        if( pGroup != NULL )
        {
            Scrooby::Text* gamelogText = pGroup->GetText( "DisplayMode" );
            Scrooby::Text* creditsText = pGroup->GetText( "ViewCredits" );
            Scrooby::Text* moviesText = pGroup->GetText( "ViewMovies" );
            if( gamelogText != NULL && creditsText != NULL )
            {
                int cWidth = 0, cHeight = 0;
                creditsText->GetBoundingBoxSize( cWidth, cHeight );
                int cCenterX = 0, cCenterY = 0;
                creditsText->GetBoundingBoxCenter( cCenterX, cCenterY );

                int spacingY = -38;
                if( moviesText != NULL )
                {
                    int mCenterX = 0, mCenterY = 0;
                    moviesText->GetBoundingBoxCenter( mCenterX, mCenterY );
                    int diff = cCenterY - mCenterY;
                    if( diff < -15 && diff > -75 )
                    {
                        spacingY = diff;
                    }
                }

                int targetWidth = ( cWidth > 320 ) ? cWidth : 320;
                int targetHeight = ( cHeight > 32 ) ? cHeight : 32;

                gamelogText->SetBoundingBoxSize( targetWidth, targetHeight );
                gamelogText->SetHorizontalJustification( creditsText->GetHorizontalJustification() );
                gamelogText->SetVerticalJustification( creditsText->GetVerticalJustification() );
                gamelogText->SetPositionOfCenter( cCenterX, cCenterY + spacingY );
                gamelogText->SetColour( creditsText->GetColour() );
                gamelogText->SetAlpha( 1.0f );
                gamelogText->SetString( 0, GameLog::IsEnabled() ? "Gamelog (On)" : "Gamelog (Off)" );
                gamelogText->SetVisible( true );
            }
        }
    }
}

void
CGuiScreenOptions::UpdateModsDisplay()
{
    Scrooby::Page* pPage = m_pScroobyScreen->GetPage( "Options" );
    if( pPage != NULL )
    {
        Scrooby::Group* pGroup = pPage->GetGroup( "Menu" );
        if( pGroup != NULL )
        {
            Scrooby::Text* modsText = pGroup->GetText( "Display" );
            Scrooby::Text* creditsText = pGroup->GetText( "ViewCredits" );
            Scrooby::Text* moviesText = pGroup->GetText( "ViewMovies" );
            if( modsText != NULL && creditsText != NULL )
            {
                int cWidth = 0, cHeight = 0;
                creditsText->GetBoundingBoxSize( cWidth, cHeight );
                int cCenterX = 0, cCenterY = 0;
                creditsText->GetBoundingBoxCenter( cCenterX, cCenterY );

                int spacingY = -38;
                if( moviesText != NULL )
                {
                    int mCenterX = 0, mCenterY = 0;
                    moviesText->GetBoundingBoxCenter( mCenterX, mCenterY );
                    int diff = cCenterY - mCenterY;
                    if( diff < -15 && diff > -75 )
                    {
                        spacingY = diff;
                    }
                }

                int targetWidth = ( cWidth > 320 ) ? cWidth : 320;
                int targetHeight = ( cHeight > 32 ) ? cHeight : 32;

                modsText->SetBoundingBoxSize( targetWidth, targetHeight );
                modsText->SetHorizontalJustification( creditsText->GetHorizontalJustification() );
                modsText->SetVerticalJustification( creditsText->GetVerticalJustification() );
                modsText->SetPositionOfCenter( cCenterX, cCenterY + 2 * spacingY );
                modsText->SetColour( creditsText->GetColour() );
                modsText->SetAlpha( 1.0f );

                char modsStr[64];
                ModManager::GetInstance().GetOptionsDisplayString( modsStr, sizeof(modsStr) );
                modsText->SetString( 0, modsStr );
                modsText->SetVisible( true );
            }
        }
    }
}

void
CGuiScreenOptions::EnterModsMenu()
{
    ModManager::GetInstance().RescanMods();
    int activeIdx = ModManager::GetInstance().GetActiveModIndex();
    m_modsMenuSelection = ( activeIdx >= 0 ) ? activeIdx : 0;
    m_modsMenuTopIndex = 0;
    m_inModLoadingState = false;
    m_inModErrorState = false;

    if( m_cheatsOverlay != NULL && m_cheatsPage != NULL )
    {
        m_inModsMenu = true;
        m_cheatsOverlay->SetVisible( true );
        this->UpdateModsMenuDisplay();
    }
    else
    {
        // Fallback: cycle mod directly
        ModManager::GetInstance().CycleMod( 1 );
        this->UpdateModsDisplay();
    }
}

void
CGuiScreenOptions::ExitModsMenu()
{
    m_inModsMenu = false;
    m_inModLoadingState = false;
    m_inModErrorState = false;
    if( m_cheatsOverlay != NULL )
    {
        m_cheatsOverlay->SetVisible( false );
    }
    this->UpdateModsDisplay();
}

void
CGuiScreenOptions::UpdateModsMenuDisplay()
{
    if( m_cheatsPage == NULL )
    {
        return;
    }

    int modCount = ModManager::GetInstance().GetModCount();
    int activeIdx = ModManager::GetInstance().GetActiveModIndex();

    const int MAX_DISPLAYED_MODS = 14;
    if( m_modsMenuSelection < m_modsMenuTopIndex )
    {
        m_modsMenuTopIndex = m_modsMenuSelection;
    }
    else if( m_modsMenuSelection >= m_modsMenuTopIndex + MAX_DISPLAYED_MODS )
    {
        m_modsMenuTopIndex = m_modsMenuSelection - MAX_DISPLAYED_MODS + 1;
    }

    Scrooby::Text* headerText = m_cheatsPage->GetText( "Cheat0" );
    if( headerText != NULL )
    {
        headerText->SetColour( tColour( 255, 200, 50 ) );
        char headerBuf[64];
        snprintf( headerBuf, sizeof(headerBuf), "=== SELECT ACTIVE MOD (%d/%d) ===",
                  modCount > 0 ? (m_modsMenuSelection + 1) : 0, modCount );
        headerText->SetString( 0, headerBuf );
    }

    for( int i = 0; i < MAX_DISPLAYED_MODS; i++ )
    {
        char name[32];
        sprintf( name, "Cheat%d", i + 1 );
        Scrooby::Text* pText = m_cheatsPage->GetText( name );
        if( pText == NULL )
        {
            continue;
        }

        pText->SetBoundingBoxSize( 580, 24 );

        int modIndex = m_modsMenuTopIndex + i;
        if( modIndex < modCount )
        {
            const ModEntry* mod = ModManager::GetInstance().GetMod( modIndex );
            char lineBuf[128];
            const char* marker = ( modIndex == activeIdx ) ? "[X]" : "[ ]";
            const char* cursor = ( modIndex == m_modsMenuSelection ) ? "> " : "  ";

            const char* title = ( mod != NULL ) ? mod->displayName.c_str() : "Unknown";
            snprintf( lineBuf, sizeof(lineBuf), "%s%s %s", cursor, marker, title );

            pText->SetString( 0, lineBuf );
            if( modIndex == m_modsMenuSelection )
            {
                pText->SetColour( tColour( 255, 255, 0 ) );
            }
            else if( modIndex == activeIdx )
            {
                pText->SetColour( tColour( 100, 255, 100 ) );
            }
            else
            {
                pText->SetColour( tColour( 200, 200, 200 ) );
            }
        }
        else
        {
            pText->SetString( 0, " " );
        }
    }

    Scrooby::Text* footerText = m_cheatsPage->GetText( "Cheat15" );
    if( footerText != NULL )
    {
        footerText->SetBoundingBoxSize( 580, 30 );
        footerText->SetColour( tColour( 180, 180, 180 ) );
        footerText->SetString( 0, "[SELECT] Activate   [BACK] Exit Menu" );
    }
}

void
CGuiScreenOptions::OnModProgressCallback( float progress, const char* currentFile, void* userData )
{
    CGuiScreenOptions* pThis = static_cast<CGuiScreenOptions*>( userData );
    if( pThis != NULL )
    {
        unsigned int now = radTimeGetMilliseconds();
        if( progress >= 1.0f || progress <= 0.01f || ( now - pThis->m_lastProgressRenderTime ) >= 33 )
        {
            pThis->m_lastProgressRenderTime = now;
            pThis->UpdateModProgress( progress, currentFile );
            GetGuiSystem()->Update( 16 );
            GetRenderManager()->ContextUpdate( 16 );
        }
    }
}

void
CGuiScreenOptions::UpdateModProgress( float progress, const char* currentFile )
{
    if( m_cheatsPage == NULL ) return;

    int pct = (int)(progress * 100.0f);
    if( pct < 0 ) pct = 0;
    if( pct > 100 ) pct = 100;

    const int BAR_WIDTH = 25;
    int filled = (pct * BAR_WIDTH) / 100;
    char bar[64];
    bar[0] = '[';
    for( int i = 0; i < BAR_WIDTH; i++ )
    {
        if( i < filled ) bar[i + 1] = '=';
        else if( i == filled && pct < 100 ) bar[i + 1] = '>';
        else bar[i + 1] = ' ';
    }
    bar[BAR_WIDTH + 1] = ']';
    snprintf( bar + BAR_WIDTH + 2, sizeof(bar) - BAR_WIDTH - 2, " %3d%%", pct );

    Scrooby::Text* t0 = m_cheatsPage->GetText( "Cheat0" );
    if( t0 )
    {
        t0->SetColour( tColour( 255, 200, 50 ) );
        t0->SetString( 0, "=== UNPACKING MOD ARCHIVE ===" );
    }

    const ModEntry* mod = ModManager::GetInstance().GetMod( m_modsMenuSelection );
    Scrooby::Text* t1 = m_cheatsPage->GetText( "Cheat1" );
    if( t1 )
    {
        t1->SetColour( tColour( 255, 255, 255 ) );
        t1->SetString( 0, mod ? mod->displayName.c_str() : "Mod" );
    }

    Scrooby::Text* t2 = m_cheatsPage->GetText( "Cheat2" );
    if( t2 )
    {
        t2->SetColour( tColour( 80, 220, 255 ) );
        t2->SetString( 0, bar );
    }

    Scrooby::Text* t3 = m_cheatsPage->GetText( "Cheat3" );
    if( t3 )
    {
        t3->SetColour( tColour( 255, 255, 120 ) );
        char fileBuf[128];
        const char* fn = currentFile ? currentFile : "";
        if( strlen(fn) > 36 ) fn += (strlen(fn) - 36);
        snprintf( fileBuf, sizeof(fileBuf), "File: %s", fn );
        t3->SetString( 0, fileBuf );
    }

    for( int i = 4; i <= 14; i++ )
    {
        char name[32];
        sprintf( name, "Cheat%d", i );
        Scrooby::Text* t = m_cheatsPage->GetText( name );
        if( t ) t->SetString( 0, " " );
    }

    Scrooby::Text* t15 = m_cheatsPage->GetText( "Cheat15" );
    if( t15 )
    {
        t15->SetColour( tColour( 255, 120, 120 ) );
        t15->SetString( 0, "Please wait... Do not exit the app." );
    }
}

void
CGuiScreenOptions::ShowModError( const char* errorMessage )
{
    m_inModLoadingState = false;
    m_inModErrorState = true;

    if( m_cheatsPage == NULL ) return;

    Scrooby::Text* t0 = m_cheatsPage->GetText( "Cheat0" );
    if( t0 )
    {
        t0->SetColour( tColour( 255, 50, 50 ) );
        t0->SetString( 0, "=== MOD ERROR ===" );
    }

    Scrooby::Text* t1 = m_cheatsPage->GetText( "Cheat1" );
    if( t1 )
    {
        t1->SetColour( tColour( 255, 100, 100 ) );
        t1->SetString( 0, "Failed to load mod. Reverted to Vanilla." );
    }

    Scrooby::Text* t2 = m_cheatsPage->GetText( "Cheat2" );
    if( t2 )
    {
        t2->SetColour( tColour( 255, 220, 220 ) );
        t2->SetString( 0, errorMessage ? errorMessage : "Unknown error occurred." );
    }

    for( int i = 3; i <= 14; i++ )
    {
        char name[32];
        sprintf( name, "Cheat%d", i );
        Scrooby::Text* t = m_cheatsPage->GetText( name );
        if( t ) t->SetString( 0, " " );
    }

    Scrooby::Text* t15 = m_cheatsPage->GetText( "Cheat15" );
    if( t15 )
    {
        t15->SetColour( tColour( 255, 255, 0 ) );
        t15->SetString( 0, "[Press SELECT or BACK to continue]" );
    }

    GetEventManager()->TriggerEvent( EVENT_FE_CHEAT_FAILURE );
}

void
CGuiScreenOptions::ActivateSelectedMod()
{
    int count = ModManager::GetInstance().GetModCount();
    if( m_modsMenuSelection < 0 || m_modsMenuSelection >= count ) return;

    const ModEntry* mod = ModManager::GetInstance().GetMod( m_modsMenuSelection );
    bool isArchive = ( mod != NULL && mod->isArchive );

    m_inModLoadingState = true;
    m_inModErrorState = false;
    m_lastProgressRenderTime = 0;

    if( isArchive )
    {
        this->UpdateModProgress( 0.0f, "Starting extraction..." );
        GetGuiSystem()->Update( 16 );
        GetRenderManager()->ContextUpdate( 16 );
    }

    bool success = ModManager::GetInstance().SetActiveMod( m_modsMenuSelection, isArchive ? OnModProgressCallback : NULL, this );

    m_inModLoadingState = false;

    if( !success )
    {
        const char* err = ModManager::GetInstance().GetLastError();
        this->ShowModError( ( err != NULL && err[0] != '\0' ) ? err : "Failed to activate mod." );
    }
    else
    {
        this->UpdateModsMenuDisplay();
        GetEventManager()->TriggerEvent( EVENT_FE_MENU_SELECT );
    }
}

void
CGuiScreenOptions::UpdateCheatsDisplay( int cheatID )
{
    if( m_cheatsPage != NULL )
    {
        // update cheats info
        //
        CheatsDB* cheatsDB = GetCheatInputSystem()->GetCheatsDB();
        rAssert( cheatsDB != NULL );

        for( unsigned int i = 0; i < NUM_CHEAT_DISPLAY_LINES; i++ )
        {
            char name[ 32 ];
            sprintf( name, "Cheat%d", i );
            Scrooby::Text* pText = m_cheatsPage->GetText( name );
            rAssert( pText != NULL );

            HeapMgr()->PushHeap( GMA_LEVEL_FE );

            if( cheatID < NUM_CHEATS )
            {
                char cheatInfo[ 64 ];

                const Cheat* cheat = cheatsDB->GetCheat( static_cast<eCheatID>( cheatID ) );
                if( cheat != NULL )
                {
                    CheatsDB::PrintCheatInfo( cheat, cheatInfo );

                    if( GetCheatInputSystem()->IsCheatEnabled( static_cast<eCheatID>( cheatID ) ) )
                    {
                        strcat( cheatInfo, "  (!)" ); // exclamation mark denotes cheat enabled
                    }
                }
                else
                {
                    // display un-registered cheat
                    //
                    sprintf( cheatInfo,
                             "Cheat ID [%02d]: *** Not Registered! ***", cheatID );
/*
                    // ignore/skip un-registered cheat and don't display anything
                    //
                    pText->SetString( 0, " " );
                    i--;
*/
                }

                pText->SetString( 0, cheatInfo );
            }
            else
            {
                pText->SetString( 0, " " );
            }

            HeapMgr()->PopHeap( GMA_LEVEL_FE );

            cheatID++;
        }
    }
}

void
CGuiScreenOptions::ProgressiveModeTestBegin()
{
    m_guiManager->DisplayMessage( CGuiScreenMessage::MSG_ID_PROGRESSIVE_SCAN_TEST, this );
    m_elapsedProgressiveModeTestTime = 0;

    rTunePrintf( "Testing progressive scan display ... ...\n" );

#ifdef RAD_PS2
    PS2Platform::GetInstance()->SetProgressiveMode( true );
#endif
}

void
CGuiScreenOptions::OnProgressiveModeTestEnd()
{
    m_guiManager->DisplayPrompt( PROMPT_DISPLAY_PROGRESSIVE_SCAN_CONFIRM, this );
    m_elapsedProgressiveModeTestTime = -1;

    rTunePrintf( "Progressive scan test completed.\n" );

#ifdef RAD_PS2
    PS2Platform::GetInstance()->SetProgressiveMode( false );
#endif
}

