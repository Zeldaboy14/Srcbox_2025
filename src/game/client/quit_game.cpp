#include "cbase.h"
#include "vgui_controls/Panel.h"
#include "vgui/ISurface.h"
#include "engine/IEngineSound.h"
#include "filesystem.h"
#include "ienginevgui.h"
#include "vgui/IVGui.h"
#include "vgui_video.h" // Includes the video player system

/*class CQuitVideoPanel : public CVideoPanel
{
    DECLARE_CLASS_SIMPLE(CQuitVideoPanel, CVideoPanel);

public:
    CQuitVideoPanel(vgui::VPANEL parent) : BaseClass(nullptr, "QuitVideoPanel")
    {
        SetParent(parent);
        SetBounds(0, 0, ScreenWidth(), ScreenHeight());
        SetVisible(true);
        SetKeyBoardInputEnabled(false);
        SetMouseInputEnabled(false);

        // Play the video
        if (StartVideo())
        {
            // Successfully started video playback
            Msg("Quit video started.\n");
        }
        else
        {
            // Fallback: If video fails to play, just quit
            Msg("Failed to play video. Exiting game.\n");
            engine->ClientCmd("quit");
        }
    }

   /* bool StartVideo()
    {
        return BeginPlayback("media/quit_video.bik"); // Load and start the video
    }

    virtual void OnVideoComplete() override
    {
        // Called when the video finishes
        Msg("Quit video finished. Exiting game...\n");
        engine->ClientCmd("quit");
    }
};*/

// Console command to play the quit video
//CON_COMMAND(custom_quit, "Plays a video before quitting the game")
//{
//    new CQuitVideoPanel(enginevgui->GetPanel(PANEL_GAMEUIDLL));
//}

CON_COMMAND(custom_quit, "Plays a WebM video before quitting the game")
{
    engine->ClientCmd_Unrestricted("playvideo quit_video.webm");
}
