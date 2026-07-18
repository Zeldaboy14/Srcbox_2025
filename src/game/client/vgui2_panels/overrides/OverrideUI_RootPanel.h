#include "vgui_controls/Panel.h"
#include "GameUI/IGameUI.h"

// This class is what is actually used instead of the main menu.
class OverrideUI_RootPanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( OverrideUI_RootPanel, vgui::Panel );
public:
	OverrideUI_RootPanel(vgui::VPANEL parent);
	virtual ~OverrideUI_RootPanel();

	IGameUI*		GetGameUI();

protected:
	virtual void	ApplySchemeSettings(vgui::IScheme *pScheme);

private:
	bool			LoadGameUI();

	int				m_ExitingFrameCount;
	bool			m_bCopyFrameBuffer;

	IGameUI*		gameui;
};

extern OverrideUI_RootPanel *guiroot;