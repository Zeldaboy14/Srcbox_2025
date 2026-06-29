#ifndef P3_COLLISIONBLOCKS
#define P3_COLLISIONBLOCKS

class CBaseAnimating;

struct	collision_description_t {

	DECLARE_SIMPLE_DATADESC();

	collision_description_t();

	void	Reset( CBaseAnimating *pOwner );
	void	ProcessCollision( IPartitionEnumerator *enumerator ); // обработать столкновения с боксом
	void	ReadCollisionInfo( char *pszSource ); // прочитать из имени аттачмента параметры бокса
	void	Remove();

	Vector	m_vecCollisionSize;
	int		m_nCollisionAttachment;
	int		m_nCollisionBlockHandle; // индекс в глобальном массиве
	Vector	m_vecCollisionPrevPos; // позиция бокса при предыдущем обновлении
	Vector	m_vecOrigin;
	CHandle<CBaseAnimating> m_hOwner; // НПЦ, которому принадлежит бокс
};

class RhinoCollisionShit
{
public:
	SF void FSpawn(CBaseAnimating* owner);
	SF void FRemove(CBaseAnimating* owner);
	bool FReadCollisionDescription(CBaseAnimating *pAnimating);

	collision_description_t m_FrontCollision, m_CenterCollision, m_BackCollision;
};

class CP3_CollisionBlocks
{
public:

	struct p3_collisionblock_t {
		p3_collisionblock_t():
		vecMins(vec3_origin),
		vecMaxs(vec3_origin),
		bUsed(false)
		{}
		Vector	vecMins;
		Vector	vecMaxs;
		bool	bUsed;
		EHANDLE	hOwner;
	};

	CP3_CollisionBlocks();

	void	Reset();
	int		Add( CBaseEntity *pOwner, const Vector &vecMins, const Vector &vecMaxs );
	void	Remove( int nBlock );
	void	Update( int nBlock, const Vector &vecMins, const Vector &vecMaxs );

	bool	IsIntersects( CBaseEntity *pOwner, const Vector &vecMins, const Vector &vecMaxs, CBaseEntity *&pOutBlockOwner ) const;
	Vector	GetRetreatDirectoin( CBaseEntity *pOwner, const Vector &vecMins, const Vector &vecMaxs ) const;

	void	ClipTraceToBlockOwners( const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, ITraceFilter *filter, trace_t *tr );
	void	FuckingFuck( const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, ITraceFilter *filter, trace_t *tr );

	bool	AddClient( CBaseEntity *pClient );
	void	RemoveClient( CBaseEntity *pClient );

protected:
	enum	{ MAX_BLOCKS_NUMBER=32 };
	enum	{ MAX_CLIENTS_NUMBER=8 };

	p3_collisionblock_t m_Blocks[MAX_BLOCKS_NUMBER];
	EHANDLE				m_Clients[MAX_CLIENTS_NUMBER];
};

bool P3IsIntersectsWithCollisionBlocks( CBaseEntity *pOwner, const Vector &vecOrigin, const Vector &vecMins, const Vector &vecMaxs, CBaseEntity *&pOutBlockOwner );

#endif	// P3_COLLISIONBLOCKS