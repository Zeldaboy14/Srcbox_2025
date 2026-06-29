#ifndef P3_FLUIDS_SHARED
#define P3_FLUIDS_SHARED

#ifdef _WIN32
#pragma once
#endif


#define P3_FLUIDS_MAX					2048
#define P3_FLUIDS_PER_NODE				256
#define P3_ASSERT_FLUID_INDEX( X )		Assert( X >= 0 && X < P3_FLUIDS_MAX	)

#define P3_FLUID_GET_TYPE( X )			( ( X & 0xF000 ) >> 12 )
#define P3_FLUID_SET_TYPE( X )			( ( X << 12 ) /* & 0xF000 */ )
enum P3_FluidType
{
	P3_FLUID_TYPE_GASOLINE		= 0,	// бензин
	P3_FLUID_TYPE_PUKE			= 1,	// блевотина
	P3_FLUID_TYPE_WEE			= 2,	// моча
	P3_FLUID_NUM_TYPES			= 3,	// #
	P3_FLUID_TYPE_ERROR			= 4,
};

enum P3_FluidCategory
{
	P3_FLUID_CATEGORY_GROUND	= 0,	// лужа на горизонтальной поверхности
	P3_FLUID_CATEGORY_CEILING	= 1,	// пятно на потолке
	P3_FLUID_CATEGORY_WALL		= 2,	// пятно на стене
	P3_FLUID_NUM_CATEGORIES		= 3,	// #
	P3_FLUID_CATEGORY_ERROR		= 4,
};

#define P3_FLUID_FLAG_ACTIVE			1	// включено
#define P3_FLUID_FLAG_FLAMMABLE			2	// может гореть
#define P3_FLUID_FLAG_BURNING			4	// горит
#define P3_FLUID_FLAG_TAKING_FIRE		8	// горит
#define P3_FLUID_FLAG_READY_TO_BURN		16	// готов гореть
#define P3_FLUID_FLAG_METABURNING		( P3_FLUID_FLAG_BURNING | P3_FLUID_FLAG_TAKING_FIRE | P3_FLUID_FLAG_READY_TO_BURN )
#define P3_FLUID_FLAG_VISIBLE			32	// видимо...
#define P3_FLUID_FLAG_IS_BURNING		64	// горит в клеточке...



class CParticleSystem;
class P3_FluidsCollection;
class CP3_FluidsCollectionNode;
class C_P3_FluidsCollectionNode;


P3_FluidCategory P3_GetFluidCategory( const Vector &normal );

//-----------------------------------------------------------------------------
// P3_FluidDesc -- параметры определенного типа жидкости
//-----------------------------------------------------------------------------

struct P3_FluidDesc
{
	const char*		texture;		// текстура
	const char*		decal;			// декаль на стенки и модели	
	IMaterial*		material;		// текстура
	float			spreadSpeed;	// скорость растекания
	float			maxRadius;		// максимальный радиус
	bool			flammable;		// может гореть
};

//-----------------------------------------------------------------------------
// P3_Fluid -- некоторое количество жидкости
//-----------------------------------------------------------------------------

class P3_Fluid
{
public:
	P3_Fluid();
	P3_Fluid( P3_FluidType type, const Vector &origin, const Vector &normal );
	~P3_Fluid();

	void			Update( float dt );

	void			Merge( const P3_Fluid &f );

	bool			IsActive() const		{ return !!(flags & P3_FLUID_FLAG_ACTIVE); }
	bool			IsFlammable() const		{ return !!(flags & P3_FLUID_FLAG_FLAMMABLE); }
	bool			IsBurning() const		{ return !!(flags & P3_FLUID_FLAG_BURNING); }
	bool			IsMetaBurning() const	{ return !!(flags & P3_FLUID_FLAG_METABURNING); }
	bool			IsTakingFire() const	{ return !!(flags & P3_FLUID_FLAG_TAKING_FIRE); }
	bool			IsReadyToBurn() const	{ return !!(flags & P3_FLUID_FLAG_READY_TO_BURN); }

	void			MakeActive()			{ flags |= P3_FLUID_FLAG_ACTIVE; }
	void			MakeFlammable()			{ flags |= P3_FLUID_FLAG_FLAMMABLE; }
	void			MakeBurning()			{ flags |= P3_FLUID_FLAG_BURNING; }
	void			Extinguish()			{ flags &= ~(P3_FLUID_FLAG_BURNING | P3_FLUID_FLAG_TAKING_FIRE | P3_FLUID_FLAG_READY_TO_BURN); }

	// networked parameters.
	float			GetAmount() const;
	float			GetVelocity() const;
	float			GetRadius() const;
	void			SetAmount( float a );
	void			SetVelocity( float v );
	void			SetRadius( float r );

	void			DrawDebugOveralay();

	int					index;
	P3_FluidsCollection	*owner;

	P3_FluidType		type;			// тип вещества
	P3_FluidCategory	category;		// категория вещества
	unsigned char		flags;			// флаги

	Vector				origin;			// позиция
	Vector				normal;			// углы
	CParticleSystem*	effects[2];		// эффекты течения и брызг

	float			maxRadius;		// размер лужи после растекания

	float			alpha;			// прозрачность (для имитации впитывания)
	float			xs, ys;			// величина сдвига нормалмапы
	float			ignitionTime;	// время, необходимое для возгорания
	float			rndBumpAngle;	// случайный угол поворота карты нормалей
	bool			flow;			// признак течения
	bool			bShowBlackSpot;	// отображать ли темное пятно под лужей
	bool			bDirCalculated;	// направление рассчитано
	Vector			dir;			// направление течения жидкости

#ifdef CLIENT_DLL
	Vector			forward;
	Vector			right;
#else
	EHANDLE			activator;
	float			burnStartTime;
	float			burnDuration;
#endif

	int				graph_index;
};

//-----------------------------------------------------------------------------
// P3_FluidsIndices -- массив с индексами
//-----------------------------------------------------------------------------

class P3_FluidsIndices
{
public:
	P3_FluidsIndices();

	int				Add( int index );
	void			Remove( int index );
	
	int				Find( int index ) const;

	void			Purge() { m_count = 0; }

	int				Count() const { return m_count; }

	int*			Pointer() { return &m_indices[0]; }

	int				Index( int i ) const;
	int				Index( int i );

	int				operator[]( int i ) const { return Index( i ); }

private:
	int				m_indices[P3_FLUIDS_MAX];
	int				m_count;
};

//-----------------------------------------------------------------------------
// Функции
//-----------------------------------------------------------------------------

void				P3_InitFluidDescs();
const P3_FluidDesc&	P3_GetFluidDesc( int type );
float				P3_CalcFluidRadius( float amount );
float				P3_CalcFluidRadius2( float amount );

#endif	// P3_FLUIDS_SHARED
