//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#ifndef __VLOADINGPROGRESS_H__
#define __VLOADINGPROGRESS_H__

#include "basemodui.h"
#include "vgui/IScheme.h"
#include "const.h"
#include "../loadingtippanel.h"

// Srcbox: loading bar style (defined in vloadingprogress.cpp)
struct LoadingBarStyle_t;

namespace BaseModUI {

class LoadingProgress : public CBaseModFrame
{
	DECLARE_CLASS_SIMPLE( LoadingProgress, CBaseModFrame );

public:
	// the full-screen image behind the loading screen
	static const char *GetBackgroundImageName();

	enum LoadingType
	{
		LT_UNDEFINED = 0,
		LT_MAINMENU,
		LT_TRANSITION,
		LT_POSTER,
	};

	enum LoadingWindowType
	{
		LWT_LOADINGPLAQUE,
		LWT_BKGNDSCREEN,
	};

#define	NUM_LOADING_CHARACTERS	4

public:
	LoadingProgress( vgui::Panel *parent, const char *panelName, LoadingWindowType eLoadingType );
	~LoadingProgress();

	virtual void		Close();

	void				SetProgress( float progress );
	float				GetProgress();

	void				SetLoadingType( LoadingType loadingType );
	LoadingType			GetLoadingType();

	bool				ShouldShowPosterForLevel( KeyValues *pMissionInfo, KeyValues *pChapterInfo );
	void				SetPosterData( KeyValues *pMissionInfo, KeyValues *pChapterInfo, const char **pPlayerNames, unsigned int botFlags, const char *pszGameMode );

	bool				IsDrawingProgressBar( void ) { return m_bDrawProgress; }

	// Srcbox: which loading bar this screen uses, per gamemode (s_GamemodeLoadingBars in
	// vloadingprogress.cpp); gamemodes not listed follow ui_loadingbar_classic.
	bool				UsesClassicLoadingBar( void ) const;
	const ::LoadingBarStyle_t *GetLoadingBarStyle( void ) const;

protected:
	virtual void		OnThink();
	virtual void		OnCommand(const char *command);
	virtual void		ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void		PaintBackground();

private:
	void				SetupControlStates( void );
	void				SetupPoster( void );

	// Srcbox: per-gamemode loading screen
	static void			GetCurrentGamemode( char *pszOut, int nOutSize );
	void				SelectGamemodeLayout( const char *pszGamemode );
	void				CheckGamemodeChanged( void );
	void				ApplyGamemodeVisuals( void );
	void				ApplyLoadingBarLayout( void );
	void				ClearControlPointers( void );
	void				UpdateWorkingAnim();
	void				RearrangeNames( const char *pszCharacterOrder, const char **pPlayerNames );

	vgui::ProgressBar	*m_pProTotalProgress;
	vgui::ImagePanel	*m_pWorkingAnim;
	vgui::ImagePanel	*m_pBGImage;
	vgui::ImagePanel	*m_pPoster; 
	vgui::EditablePanel *m_pFooter;
	LoadingType			m_LoadingType;
	LoadingWindowType	m_LoadingWindowType;

	bool				m_bFullscreenPoster;

	// Poster Data
	char				m_PlayerNames[NUM_LOADING_CHARACTERS][MAX_PLAYER_NAME_LENGTH];
	KeyValues			*m_pMissionInfo;
	KeyValues			*m_pChapterInfo;
	KeyValues			*m_pDefaultPosterDataKV;
	int					m_botFlags;
	bool				m_bValid;

	int					m_textureID_LoadingBar;
	int					m_textureID_LoadingBarBG;
	int					m_textureID_DefaultPosterImage;

	bool				m_bDrawBackground;
	bool				m_bDrawPoster;
	bool				m_bDrawProgress;
	bool				m_bDrawSpinner;

	float				m_flPeakProgress;

	float				m_flLastEngineTime;
	int					m_nSpinnerFrame;

	char				m_szGameMode[MAX_PATH];

	// Srcbox: per-gamemode loading screen
	char				m_szLayoutGamemode[64];		// gamemode the current layout/visuals were built for
	char				m_szBGTexture[MAX_PATH];	// material drawn as the background
	int					m_nBGTextureID;
	float				m_flNextGamemodeCheck;

};

};

#endif // __VLOADINGPROGRESS_H__