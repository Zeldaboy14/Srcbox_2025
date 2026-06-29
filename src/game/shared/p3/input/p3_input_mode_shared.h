#ifndef P3_INPUT_MODE_SHARED_H
#define P3_INPUT_MODE_SHARED_H

//-----------------------------------------------------------------------------
// InputMode_t
//-----------------------------------------------------------------------------

enum InputMode_t
{
	INPUT_MODE_THIRDPERSON,				// вид от третьего лица
	INPUT_MODE_FIRSTPERSON,				// вид от первого лица
	INPUT_MODE_FREECAMERA,				// игрок стоит на месте, летает камера
	INPUT_MODE_THIRDPERSON_AIM,			// прицеливание от третьего лица
	INPUT_MODE_THIRDPERSON_COVER,		// прятанье за препятствиями
	INPUT_MODE_THIRDPERSON_COVER_AIM,	// стрельба из-за препятствий
	INPUT_MODE_CUTSCENE,				// катсценная камера
	INPUT_MODE_KILLING,					// пилит барсуком или еще чем	
	INPUT_MODE_THIRDPERSON_ALT1,		// третье лицо, камера летает вокруг	
	INPUT_MODE_THIRDPERSON_ATL2,		// третье лицо, камера стоит на месте
	INPUT_MODE_THIRDPERSON_BURST,		// быстрый бег от третьего лица
	INPUT_MODE_THIRDPERSON_HOSTAGE,		// движение с заложником
	INPUT_MODE_THIRDPERSON_SEGWAY,		// едет на сигвее
	INPUT_MODE_THIRDPERSON_DEAD,		// умер чувак
	INPUT_MODE_THIRDPERSON_VEHICLE,		
	INPUT_MODE_THIRDPERSON_KICKED,		// чувака кикнули

	NUM_INPUT_MODES
};

//-----------------------------------------------------------------------------
// Функции
//-----------------------------------------------------------------------------

InputMode_t	GetInputMode();
void		SetInputMode( InputMode_t mode );
inline bool	IsAimingMode( int mode ) { return mode == INPUT_MODE_THIRDPERSON_AIM || mode == INPUT_MODE_THIRDPERSON_COVER_AIM; }

#endif  // P3_INPUT_MODE_SHARED_H