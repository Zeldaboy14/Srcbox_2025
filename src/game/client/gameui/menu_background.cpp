//========= Copyright Jorge "BSVino" Rodriguez, All rights reserved. ============//
//
// Purpose: Animated menu background
//
//====================================================================================//

#include "cbase.h"
#include "menu_background.h"
#include <cdll_client_int.h>
#include <ienginevgui.h>
#include <KeyValues.h>
#include "iclientmode.h"

#include <vgui/IScheme.h>
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>

#include "clienteffectprecachesystem.h"
#include "tier0/icommandline.h"
#include "fmtstr.h"
#include "baseviewport.h"
#if TF_CLIENT_DLL
#include "tf/tf_hud_mainmenuoverride.h"
#include "tf/tf_shareddefs.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// ???


static bool bOverrideVideo = false;
static char* bOverrideVideoBuf = NULL;
static char* bOverrideVideoSongBuf = NULL;
static float flVolume = NULL;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CAnimatedBackgroundMovie::CAnimatedBackgroundMovie(vgui::Panel* parent, const char* pElementName) : vgui::Panel(NULL, "MainMenu")
{
	vgui::VPANEL pParent = enginevgui->GetPanel(PANEL_GAMEUIDLL);
	SetParent(pParent);
	SetBuildModeEditable(false);
	SetVisible(false);
	SetPaintEnabled(false);
	SetProportional(true);
	SetKeyBoardInputEnabled(false);
	SetPaintBorderEnabled(false);
	m_VideoMaterial = NULL;
	m_pBackgroundTexture = NULL;
	m_flMovieFadeInTime = 0.0f;
	m_nPlaybackWidth = 0;
	m_nPlaybackHeight = 0;

	m_bToolsMode = false;
	m_bGamepadUIMode = false;
	m_bLoaded = false;
	bInit = false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CAnimatedBackgroundMovie::~CAnimatedBackgroundMovie()
{
	ReleaseVideo();
	MarkForDeletion();

	if (m_DrawLogoText1)
		free(m_DrawLogoText1);

	if (m_DrawLogoText2)
		free(m_DrawLogoText2);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAnimatedBackgroundMovie::ApplySchemeSettings(IScheme* pScheme)
{
	if (bInit)
		return;

	bInit = true;
	SetPos(-1, -1);
	SetSize(ScreenWidth() + 2, ScreenHeight() + 2);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CAnimatedBackgroundMovie::IsVideoPlaying()
{
	return m_bPaintVideo;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAnimatedBackgroundMovie::StartVideo()
{
	m_bToolsMode = (IsPC() && (CommandLine()->CheckParm("-tools") != NULL)) ? true : false;

	SetVisible(true);
	SetPaintEnabled(true);

	ConVarRef sv_unlockedchapters("sv_unlockedchapters");

	// If an override video was specified, play that. Otherwise pick a random .webm from media/menubackgrounds
	if (bOverrideVideo && bOverrideVideoBuf)
	{
		if (BeginPlayback(bOverrideVideoBuf))
			m_bLoaded = true;
	}
	else
	{
		FileFindHandle_t fh;
		const char* p = g_pFullFileSystem->FindFirstEx("media/menubackgrounds/*.webm", "MOD", &fh);
		int total = 0;

		while (p)
		{
			++total;
			p = g_pFullFileSystem->FindNext(fh);
		}
		g_pFullFileSystem->FindClose(fh);

		if (total > 0)
		{
			int idx = (int)(Plat_FloatTime() * 1000.0f) % total;
			int cur = 0;
			char selected[MAX_PATH] = { 0 };

			p = g_pFullFileSystem->FindFirstEx("media/menubackgrounds/*.webm", "MOD", &fh);
			while (p)
			{
				if (cur == idx)
				{
					if (Q_strnicmp(p, "media/menubackgrounds/", 6) == 0)
						Q_strncpy(selected, p, sizeof(selected));
					else
						Q_snprintf(selected, sizeof(selected), "media/menubackgrounds/%s", p);
					break;
				}
				++cur;
				p = g_pFullFileSystem->FindNext(fh);
			}
			g_pFullFileSystem->FindClose(fh);

			if (selected[0] != '\0')
			{
				if (BeginPlayback(selected))
					m_bLoaded = true;
				else
				{
					if (BeginPlayback("media/valve.webm"))
						m_bLoaded = true;
				}
			}
			else
			{
				if (BeginPlayback("media/valve.webm"))
					m_bLoaded = true;
			}
		}
		else
		{
			if (BeginPlayback("media/valve.webm"))
				m_bLoaded = true;
		}
	}

	if (bOverrideVideoBuf)
	{
		free(bOverrideVideoBuf);
		bOverrideVideoBuf = NULL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAnimatedBackgroundMovie::StopVideo()
{
	// NOTE(Tony): Release the video when stopping
	// NOTE(Tony): Modified, don't hide the panel anymore, because we draw the main menu logo thing for in-game
	//SetVisible( false ); 
	//SetPaintEnabled( false );
	ReleaseVideo();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAnimatedBackgroundMovie::GetPanelPos(int& xpos, int& ypos)
{
	vgui::ipanel()->GetAbsPos(GetVPanel(), xpos, ypos);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAnimatedBackgroundMovie::Paint(void)
{
	m_bGamepadUIMode = (IsPC() && (CommandLine()->CheckParm("-gamepadui") != NULL)) ? true : false;
	if (m_bToolsMode)
		return;

	if (engine->IsConnected())
		return;

	if (!m_bLoaded)
		return;

	if (m_bPaintVideo)
	{
		m_nPlaybackHeight = ScreenHeight() + 2;
		m_nPlaybackWidth = ScreenWidth() + 2;

		// No video to play, so do nothing
		if (m_VideoMaterial == NULL)
			return;

		// Update our frame
		if (m_VideoMaterial->Update() == false)
			return;

		float cur_vidtime = m_VideoMaterial->GetCurrentVideoTime();
		float dur_vidtime = m_VideoMaterial->GetVideoDuration();
		if ((cur_vidtime + 0.1) >= dur_vidtime)
			m_VideoMaterial->SetTime(0);

		// Sit in the "center"
		int xpos, ypos;
		GetPanelPos(xpos, ypos);

		// Draw the polys to draw this out
		CMatRenderContextPtr pRenderContext(materials);

		pRenderContext->MatrixMode(MATERIAL_VIEW);
		pRenderContext->PushMatrix();
		pRenderContext->LoadIdentity();

		pRenderContext->MatrixMode(MATERIAL_PROJECTION);
		pRenderContext->PushMatrix();
		pRenderContext->LoadIdentity();

		pRenderContext->Bind(m_pMaterial, NULL);

		CMeshBuilder meshBuilder;
		IMesh* pMesh = pRenderContext->GetDynamicMesh(true);
		meshBuilder.Begin(pMesh, MATERIAL_QUADS, 1);

		float flLeftX = xpos;
		float flRightX = xpos + (m_nPlaybackWidth - 1);

		float flTopY = ypos;
		float flBottomY = ypos + (m_nPlaybackHeight - 1);

		// Map our UVs to cut out just the portion of the video we're interested in
		float flLeftU = 0.0f;
		float flTopV = 0.0f;

		// We need to subtract off a pixel to make sure we don't bleed
		float flRightU = m_flU - (1.0f / (float)m_nPlaybackWidth);
		float flBottomV = m_flV - (1.0f / (float)m_nPlaybackHeight);

		// Get the current viewport size
		int vx, vy, vw, vh;
		pRenderContext->GetViewport(vx, vy, vw, vh);

		// Map from screen pixel coords to -1..1
		flRightX = FLerp(-1, 1, 0, vw, flRightX);
		flLeftX = FLerp(-1, 1, 0, vw, flLeftX);
		flTopY = FLerp(1, -1, 0, vh, flTopY);
		flBottomY = FLerp(1, -1, 0, vh, flBottomY);

		float alpha = ((float)GetFgColor()[3] / 255.0f);

		for (int corner = 0; corner < 4; corner++)
		{
			bool bLeft = (corner == 0) || (corner == 3);
			meshBuilder.Position3f((bLeft) ? flLeftX : flRightX, (corner & 2) ? flBottomY : flTopY, 0.0f);
			meshBuilder.Normal3f(0.0f, 0.0f, 1.0f);
			meshBuilder.TexCoord2f(0, (bLeft) ? flLeftU : flRightU, (corner & 2) ? flBottomV : flTopV);
			meshBuilder.TangentS3f(0.0f, 1.0f, 0.0f);
			meshBuilder.TangentT3f(1.0f, 0.0f, 0.0f);
			meshBuilder.Color4f(1.0f, 1.0f, 1.0f, alpha);
			meshBuilder.AdvanceVertex();
		}

		meshBuilder.End();
		pMesh->Draw();

		pRenderContext->MatrixMode(MATERIAL_VIEW);
		pRenderContext->PopMatrix();

		pRenderContext->MatrixMode(MATERIAL_PROJECTION);
		pRenderContext->PopMatrix();

		surface()->DrawSetColor(255, 255, 255, 255);
		int x, y, w, h;
		GetBounds(x, y, w, h);

		// center, aspect ratio
		/*int width_at_ratio = h * BackgroundMovieNew()->AspectRatio();
		x = (w * 0.5f) - (width_at_ratio * 0.5f);
		width_at_ratio /= BackgroundMovieNew()->MaxU();
		h /= BackgroundMovieNew()->MaxV();

		surface()->DrawTexturedRect(x, y, x + width_at_ratio, y + h);*/

		if (!m_flMovieFadeInTime)
		{
			// do the fade a little bit after the movie starts (needs to be stable)
			// the product overlay will fade out
			m_flMovieFadeInTime = Plat_FloatTime() + TRANSITION_TO_MOVIE_DELAY_TIME;
		}

		float flFadeDelta = RemapValClamped(Plat_FloatTime(), m_flMovieFadeInTime, m_flMovieFadeInTime + TRANSITION_TO_MOVIE_FADE_TIME, 1.0f, 0.0f);
		if (flFadeDelta > 0.0f)
		{
			if (!m_pMaterial)
			{
				PrepareStartupGraphicNew();
			}
			DrawStartupGraphicNew(flFadeDelta);
		}

		if (engine->IsDrawingLoadingImage())
			return;

		if (m_bGamepadUIMode)
			return;
	}
}

#ifdef SWARM_INTERFACE

// we have to draw the startup fade graphic using this function so it perfectly matches the one drawn by the engine during load
void DrawScreenSpaceRectangleAlphaMenu(IMaterial* pMaterial,
	int nDestX, int nDestY, int nWidth, int nHeight,	// Rect to draw into in screen space
	float flSrcTextureX0, float flSrcTextureY0,		// which texel you want to appear at destx/y
	float flSrcTextureX1, float flSrcTextureY1,		// which texel you want to appear at destx+width-1, desty+height-1
	int nSrcTextureWidth, int nSrcTextureHeight,		// needed for fixup
	void* pClientRenderable,							// Used to pass to the bind proxies
	int nXDice, int nYDice,							// Amount to tessellate the mesh
	float fDepth, float flAlpha)									// what Z value to put in the verts (def 0.0)
{
	CMatRenderContextPtr pRenderContext(g_pMaterialSystem);

	if ((nWidth <= 0) || (nHeight <= 0))
		return;

	pRenderContext->MatrixMode(MATERIAL_VIEW);
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	pRenderContext->MatrixMode(MATERIAL_PROJECTION);
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	pRenderContext->Bind(pMaterial, pClientRenderable);

	int xSegments = MAX(nXDice, 1);
	int ySegments = MAX(nYDice, 1);

	CMeshBuilder meshBuilder;

	IMesh* pMesh = pRenderContext->GetDynamicMesh(true);
	meshBuilder.Begin(pMesh, MATERIAL_QUADS, xSegments * ySegments);

	int nScreenWidth, nScreenHeight;
	pRenderContext->GetRenderTargetDimensions(nScreenWidth, nScreenHeight);
	float flLeftX = nDestX - 0.5f;
	float flRightX = nDestX + nWidth - 0.5f;

	float flTopY = nDestY - 0.5f;
	float flBottomY = nDestY + nHeight - 0.5f;

	float flSubrectWidth = flSrcTextureX1 - flSrcTextureX0;
	float flSubrectHeight = flSrcTextureY1 - flSrcTextureY0;

	float flTexelsPerPixelX = (nWidth > 1) ? flSubrectWidth / (nWidth - 1) : 0.0f;
	float flTexelsPerPixelY = (nHeight > 1) ? flSubrectHeight / (nHeight - 1) : 0.0f;

	float flLeftU = flSrcTextureX0 + 0.5f - (0.5f * flTexelsPerPixelX);
	float flRightU = flSrcTextureX1 + 0.5f + (0.5f * flTexelsPerPixelX);
	float flTopV = flSrcTextureY0 + 0.5f - (0.5f * flTexelsPerPixelY);
	float flBottomV = flSrcTextureY1 + 0.5f + (0.5f * flTexelsPerPixelY);

	float flOOTexWidth = 1.0f / nSrcTextureWidth;
	float flOOTexHeight = 1.0f / nSrcTextureHeight;
	flLeftU *= flOOTexWidth;
	flRightU *= flOOTexWidth;
	flTopV *= flOOTexHeight;
	flBottomV *= flOOTexHeight;

	// Get the current viewport size
	int vx, vy, vw, vh;
	pRenderContext->GetViewport(vx, vy, vw, vh);

	// map from screen pixel coords to -1..1
	flRightX = FLerp(-1, 1, 0, vw, flRightX);
	flLeftX = FLerp(-1, 1, 0, vw, flLeftX);
	flTopY = FLerp(1, -1, 0, vh, flTopY);
	flBottomY = FLerp(1, -1, 0, vh, flBottomY);

	// Dice the quad up...
	if (xSegments > 1 || ySegments > 1)
	{
		// Screen height and width of a subrect
		float flWidth = (flRightX - flLeftX) / (float)xSegments;
		float flHeight = (flTopY - flBottomY) / (float)ySegments;

		// UV height and width of a subrect
		float flUWidth = (flRightU - flLeftU) / (float)xSegments;
		float flVHeight = (flBottomV - flTopV) / (float)ySegments;

		for (int x = 0; x < xSegments; x++)
		{
			for (int y = 0; y < ySegments; y++)
			{
				// Top left
				meshBuilder.Position3f(flLeftX + (float)x * flWidth, flTopY - (float)y * flHeight, fDepth);
				meshBuilder.Normal3f(0.0f, 0.0f, 1.0f);
				meshBuilder.TexCoord2f(0, flLeftU + (float)x * flUWidth, flTopV + (float)y * flVHeight);
				meshBuilder.TangentS3f(0.0f, 1.0f, 0.0f);
				meshBuilder.TangentT3f(1.0f, 0.0f, 0.0f);
				meshBuilder.Color4ub(255, 255, 255, 255.0f * flAlpha);
				meshBuilder.AdvanceVertex();

				// Top right (x+1)
				meshBuilder.Position3f(flLeftX + (float)(x + 1) * flWidth, flTopY - (float)y * flHeight, fDepth);
				meshBuilder.Normal3f(0.0f, 0.0f, 1.0f);
				meshBuilder.TexCoord2f(0, flLeftU + (float)(x + 1) * flUWidth, flTopV + (float)y * flVHeight);
				meshBuilder.TangentS3f(0.0f, 1.0f, 0.0f);
				meshBuilder.TangentT3f(1.0f, 0.0f, 0.0f);
				meshBuilder.Color4ub(255, 255, 255, 255.0f * flAlpha);
				meshBuilder.AdvanceVertex();

				// Bottom right (x+1), (y+1)
				meshBuilder.Position3f(flLeftX + (float)(x + 1) * flWidth, flTopY - (float)(y + 1) * flHeight, fDepth);
				meshBuilder.Normal3f(0.0f, 0.0f, 1.0f);
				meshBuilder.TexCoord2f(0, flLeftU + (float)(x + 1) * flUWidth, flTopV + (float)(y + 1) * flVHeight);
				meshBuilder.TangentS3f(0.0f, 1.0f, 0.0f);
				meshBuilder.TangentT3f(1.0f, 0.0f, 0.0f);
				meshBuilder.Color4ub(255, 255, 255, 255.0f * flAlpha);
				meshBuilder.AdvanceVertex();

				// Bottom left (y+1)
				meshBuilder.Position3f(flLeftX + (float)x * flWidth, flTopY - (float)(y + 1) * flHeight, fDepth);
				meshBuilder.Normal3f(0.0f, 0.0f, 1.0f);
				meshBuilder.TexCoord2f(0, flLeftU + (float)x * flUWidth, flTopV + (float)(y + 1) * flVHeight);
				meshBuilder.TangentS3f(0.0f, 1.0f, 0.0f);
				meshBuilder.TangentT3f(1.0f, 0.0f, 0.0f);
				meshBuilder.Color4ub(255, 255, 255, 255.0f * flAlpha);
				meshBuilder.AdvanceVertex();
			}
		}
	}
	else // just one quad
	{
		for (int corner = 0; corner < 4; corner++)
		{
			bool bLeft = (corner == 0) || (corner == 3);
			meshBuilder.Position3f((bLeft) ? flLeftX : flRightX, (corner & 2) ? flBottomY : flTopY, fDepth);
			meshBuilder.Normal3f(0.0f, 0.0f, 1.0f);
			meshBuilder.TexCoord2f(0, (bLeft) ? flLeftU : flRightU, (corner & 2) ? flBottomV : flTopV);
			meshBuilder.TangentS3f(0.0f, 1.0f, 0.0f);
			meshBuilder.TangentT3f(1.0f, 0.0f, 0.0f);
			meshBuilder.Color4ub(255, 255, 255, 255.0f * flAlpha);
			meshBuilder.AdvanceVertex();
		}
	}

	meshBuilder.End();
	pMesh->Draw();

	pRenderContext->MatrixMode(MATERIAL_VIEW);
	pRenderContext->PopMatrix();

	pRenderContext->MatrixMode(MATERIAL_PROJECTION);
	pRenderContext->PopMatrix();
}

IVTFTexture* LoadVTFMenu(CUtlBuffer& temp, const char* szFileName)
{
	if (!g_pFullFileSystem->ReadFile(szFileName, NULL, temp))
		return NULL;

	IVTFTexture* texture = CreateVTFTexture();
	if (!texture->Unserialize(temp))
	{
		Error("Invalid or corrupt background texture %s\n", szFileName);
		return NULL;
	}
	texture->ConvertImageFormat(IMAGE_FORMAT_RGBA8888, false);
	return texture;
}

void CAnimatedBackgroundMovie::PrepareStartupGraphicNew()
{
	CUtlBuffer buf;
	// load in the background vtf
	buf.Clear();
	m_pBackgroundTexture = LoadVTFMenu(buf, m_szFadeFilename);
	if (!m_pBackgroundTexture)
	{
		Error("Can't find background image '%s'\n", m_szFadeFilename);
		return;
	}

	// Allocate a white material
	m_pVMTKeyValues = new KeyValues("UnlitGeneric");
	m_pVMTKeyValues->SetString("$basetexture", m_szFadeFilename + 10);
	m_pVMTKeyValues->SetInt("$ignorez", 1);
	m_pVMTKeyValues->SetInt("$nofog", 1);
	m_pVMTKeyValues->SetInt("$no_fullbright", 1);
	m_pVMTKeyValues->SetInt("$nocull", 1);
	m_pVMTKeyValues->SetInt("$vertexalpha", 1);
	m_pVMTKeyValues->SetInt("$vertexcolor", 1);
	m_pMaterial = g_pMaterialSystem->CreateMaterial("__background", m_pVMTKeyValues);
}

void CAnimatedBackgroundMovie::ReleaseStartupGraphicNew()
{
	if (m_pMaterial)
	{
		m_pMaterial->Release();
	}

	if (m_pBackgroundTexture)
	{
		DestroyVTFTexture(m_pBackgroundTexture);
		m_pBackgroundTexture = NULL;
	}
}

void CAnimatedBackgroundMovie::DrawStartupGraphicNew(float flNormalizedAlpha)
{
//#ifdef COUNTERSTRIKEFORKIDS
	CMatRenderContextPtr pRenderContext(g_pMaterialSystem);
	int w = GetWide();
	int h = GetTall();
	//int tw = m_pBackgroundTexture->Width();
	//int th = m_pBackgroundTexture->Height();

	float depth = 0.5f;
	int width_at_ratio = h * (16.0f / 9.0f);
	int x = (w * 0.5f) - (width_at_ratio * 0.5f);
	DrawScreenSpaceRectangleAlphaMenu(m_pMaterial, x, 0, width_at_ratio, h, 8, 8, 100 - 8, 100 - 8, 100, 100, NULL, 1, 1, depth, flNormalizedAlpha);
//#endif
}

#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CAnimatedBackgroundMovie::BeginPlayback(const char* pFilename)
{
	// Need working video services
//	if (g_pVideo == NULL)
//		return false;

	// Destroy any previously allocated video
	if (m_VideoMaterial != NULL)
	{
		g_pVideo->DestroyVideoMaterial(m_VideoMaterial);
		m_VideoMaterial = NULL;
	}

	// Create new Video material
	m_VideoMaterial = g_pVideo->CreateVideoMaterial("VideoMaterial", pFilename, "GAME",
		VideoPlaybackFlags::DEFAULT_MATERIAL_OPTIONS,
		VideoSystem::DETERMINE_FROM_FILE_EXTENSION, m_bAllowAlternateMedia);
	if (m_VideoMaterial == NULL)
		return false;

	m_bPaintVideo = true;

	m_VideoMaterial->SetLooping(true);

	int nWidth, nHeight;
	m_VideoMaterial->GetVideoImageSize(&nWidth, &nHeight);
	m_VideoMaterial->GetVideoTexCoordRange(&m_flU, &m_flV);
	m_pMaterial = m_VideoMaterial->GetMaterial();

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAnimatedBackgroundMovie::ReleaseVideo()
{
	m_bPaintVideo = false;

	// NOTE(Tony): Not touching the sound!!
	//enginesound->NotifyEndMoviePlayback();

	// Destroy any previously allocated video
	// Shut down this video, destroy the video material
	if (g_pVideo != NULL && m_VideoMaterial != NULL)
	{
		g_pVideo->DestroyVideoMaterial(m_VideoMaterial);
		m_VideoMaterial = NULL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAnimatedBackgroundMovie::DoModal()
{
	vgui::surface()->RestrictPaintToSinglePanel(GetVPanel());
}

//-----------------------------------------------------------------------------
// Purpose: If we get disconnected, load the menu
//-----------------------------------------------------------------------------
void CAnimatedBackgroundMovie::OnDisconnectFromGame()
{
	StartVideo();
}

//-----------------------------------------------------------------------------
// Purpose: Find the disconnect command, and rename it...
//-----------------------------------------------------------------------------
CON_COMMAND(__disconnect, "Disconnect game from server.")
{
#if TF_CLIENT_DLL
	IViewPortPanel* pMMOverride = (gViewPortInterface->FindPanelByName(PANEL_MAINMENUOVERRIDE));
	if (pMMOverride)
		((CHudMainMenuOverride*)pMMOverride)->StartMainMenuVideo();
#endif

	engine->ClientCmd_Unrestricted("__real_disconnect");

	if (bOverrideVideoSongBuf)
	{
		free(bOverrideVideoSongBuf);
		bOverrideVideoSongBuf = NULL;
	}
	bOverrideVideo = false;
}

void CC_MapBackground(const CCommand& args)
{
	if (args.ArgC() < 2)
	{
		ConMsg("Command Usage: map_background <background video> <override song> <override volume> <override>. If the <override> parameter is 1 then it will override the background video to play to the inputted background video at the 2nd argument, it will also play the song of the 3rd parameter with the volume of the 4th parameter. else it will just play the normal background video (the one that would play if you disconnected)\n");
		return;
	}

	if (atoi(args.Arg(4)) != 0)
	{
		bOverrideVideo = true;
		bOverrideVideoBuf = _strdup(args.Arg(1));
		bOverrideVideoSongBuf = _strdup(args.Arg(2));
		flVolume = atof(args.Arg(3));
	}

	__disconnect(CCommand{});
}

void SwapDisconnectCommand()
{
	//DevMsg("SwapDisconnectCommand\n");
	ConCommand* _realDisconnectCommand = dynamic_cast<ConCommand*>(g_pCVar->FindCommand("disconnect"));
	ConCommand* _DisconnectCommand = dynamic_cast<ConCommand*>(g_pCVar->FindCommand("__disconnect"));
	ConCommand* _Startupmenu = dynamic_cast<ConCommand*>(g_pCVar->FindCommand("startupmenu"));
	ConCommand* map_background = dynamic_cast<ConCommand*>(g_pCVar->FindCommand("map_background"));

	if (!_realDisconnectCommand)
		return;

	if (!_DisconnectCommand)
		return;

	_realDisconnectCommand->Shutdown();
	_realDisconnectCommand->CreateBase("__real_disconnect", "");
	_realDisconnectCommand->Init();

	_DisconnectCommand->Shutdown();
	_DisconnectCommand->CreateBase("disconnect", "Disconnect game from server.");
	_DisconnectCommand->Init();

	if (_Startupmenu)
	{
		_Startupmenu->m_bHasCompletionCallback = false;
		_Startupmenu->m_fnCommandCallback = __disconnect;
	}

	if (map_background)
	{
		map_background->Shutdown();
		map_background->CreateBase("map_background", "Plays the background video that would play when you disconnect/goto the main menu. execute this command with no args for command explination\n");
		map_background->Init();
		map_background->m_bHasCompletionCallback = false;
		map_background->m_fnCommandCallback = CC_MapBackground;
	}
}