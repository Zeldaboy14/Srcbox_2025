//========= Copyright Jorge "BSVino" Rodriguez, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef MENU_BACKGROUND_H
#define MENU_BACKGROUND_H
#ifdef _WIN32
#pragma once
#endif

#include <vguitextwindow.h>
#include "video/ivideoservices.h"
#include <materialsystem/MaterialSystemUtil.h>

// the attract screen also uses this so it doesn't pop in either
#define TRANSITION_TO_MOVIE_DELAY_TIME	0.5f	// how long to wait before starting the fade
#define TRANSITION_TO_MOVIE_FADE_TIME	1.2f	// how fast to fade

class CAnimatedBackgroundMovie : public vgui::Panel
{
public:
	DECLARE_CLASS_SIMPLE(CAnimatedBackgroundMovie, vgui::Panel);

	CAnimatedBackgroundMovie(vgui::Panel* parent, const char* pElementName);
	~CAnimatedBackgroundMovie();

public:
	virtual void ApplySchemeSettings(vgui::IScheme* pScheme);

	bool IsVideoPlaying();
	void StartVideo();
	void StopVideo();

	void GetPanelPos(int& xpos, int& ypos);

	void Paint();
	bool BeginPlayback(const char* pFilename);
	void ReleaseVideo();
	void DoModal();

	MESSAGE_FUNC(OnDisconnectFromGame, "DisconnectedFromGame");

	void PrepareStartupGraphicNew();
	void ReleaseStartupGraphicNew();
	void DrawStartupGraphicNew(float flNormalizedAlpha);

	float MaxU() { if (m_flMaxU == 0) return 1.0f; return m_flMaxU; }
	float MaxV() { if (m_flMaxV == 0) return 1.0f; return m_flMaxV; }
	float AspectRatio() { return m_flAspectRatio; }
private:
	float m_flMaxU, m_flMaxV, m_flAspectRatio;
	int m_DrawLogoX1, m_DrawLogoY1;
	int m_DrawLogoX2, m_DrawLogoY2;
	color32 m_DrawLogoColor1;
	color32 m_DrawLogoColor2;
	wchar_t* m_DrawLogoText1;
	wchar_t* m_DrawLogoText2;
	vgui::HFont m_DrawLogoFont;
	char m_szFadeFilename[MAX_PATH];

public:
	bool bInit;

private:
	bool m_bLoaded;
	bool m_bToolsMode;
	bool m_bGamepadUIMode;
	bool m_bPaintVideo;

protected:
	IVideoMaterial* m_VideoMaterial;
	IMaterial* m_pMaterial;
	IVTFTexture* m_pBackgroundTexture;
	KeyValues* m_pVMTKeyValues;

	int m_nPlaybackHeight; // Calculated to address ratio changes
	int m_nPlaybackWidth;
	char m_szExitCommand[MAX_PATH]; // This call is fired at the engine when the video finishes or is interrupted

	float m_flU; // UV ranges for video on its sheet
	float m_flV;
	float m_flMovieFadeInTime;

	bool m_bAllowAlternateMedia;
};

#endif // MENU_BACKGROUND_H
