//============================= TM-Studios ===================================//
// Author: Victor Rykov
// Note: Ballistic weapons prediction and control
//============================================================================//
#ifndef P3_BALLISTIC_CONTROLLER_H
#define P3_BALLISTIC_CONTROLLER_H
//----------------------------------------------------------------------------------------
//
//					IBcControllable
//
//----------------------------------------------------------------------------------------
class IBcControllable
{
public:
	virtual bool CanBeControlled()const = 0;
	virtual void OnBcHit(const trace_t& tr) = 0;
	virtual void OnBcTimeout() = 0;
};
//----------------------------------------------------------------------------------------
//
//					BcControlPoint
//
//----------------------------------------------------------------------------------------
struct BcControlPoint
{
	Vector	m_Position;
	Vector	m_Velocity;
	float	m_Time;
};
//----------------------------------------------------------------------------------------
//
//					CP3_BallisticController
//
//----------------------------------------------------------------------------------------
class CP3_BallisticController
{
public:

	CP3_BallisticController();
	CP3_BallisticController(const CP3_BallisticController &copy);

	void			ComputePoint(float curTime, BcControlPoint& point);

	//initialize the controller
	//dir - throw direction
	//phaseAngle - additional slope to the throw direction in degrees
	//velocityMin - minimum velocity, corresponds to zero throw angle
	//velocityMax - maximum velocity, corresponds to maximum throw angle
	//initPos - initial controlled entity position
	void			Init(const Vector& dir, float velocityMin, float velocityMax, const Vector& initPos);
	void			Init(const Vector& dir, float phaseAngle, float velocityMin, float velocityMax, const Vector& initPos);
	void			Init(const Vector& initVelocity, const Vector& initPos);
	void			SetGravity(float gravity);
	void			SetControlledEntity(CBaseEntity* ent);
	void			SetIgnoreEntities(CBaseEntity* owner, CBaseEntity* item);
	void			SetTraceHull(const Vector& mins, const Vector& maxs);
	float			GetMaxDimension();
	CBaseEntity*	GetControlledEntity()const;
	void			PredictTrajectory();
	float			GetThrowAngle();
	const Vector&	GetInitialVelocity()const;
	const Vector&	GetInitialPosition()const;
	void			Fire();
	void			Fly();
	
	void			DrawTrajectory();
	void			SetMaxSteps( int n );
	void			InitForPlayer( CBasePlayer* pPlayer );

	const CUtlVector<BcControlPoint>& GeTrajectory() { return m_Trajectory; }
	int				GetTrajectorySteps() { return m_TrajectorySteps; }
	const trace_t&	GetHitTrace() { return m_HitTrace; }

private:

	void	Construct();
	bool	IsClient();
	void	Trace(const Vector& from, const Vector& to, trace_t* tr);
	void	ComputeThrowAngle(const Vector& dir);
	float	ComputeThrowAngle(const Vector& dir, float phaseAngle, Vector& newDir);
	void	ComputeFlyTime(float velocity);

	EHANDLE						m_ControlledEnt;	
	EHANDLE						m_OwnerEnt;
	EHANDLE						m_ItemEnt;
	Vector						m_Gravity;
	float						m_GravLength;
	float						m_ThrowAngle;
	float						m_EstimatedTime;
	bool						m_TraceHull;
	Vector						m_HullMins;
	Vector						m_HullMaxs;
	Vector						m_InitialVelocity;
	Vector						m_InitialPosition;
	CUtlVector<BcControlPoint>	m_Trajectory;
	int							m_TrajectorySteps;
	int							m_CurrentWaypoint;
	float						m_FlyTime;
	int							m_MaxSteps;	
	trace_t						m_HitTrace;
};
//----------------------------------------------------------------------------------------
//
//					CP3_BallisticControllerCache
//
//----------------------------------------------------------------------------------------
class CP3_BallisticControllerCache: CAutoGameSystemPerFrame
{
public:
	CP3_BallisticControllerCache();
	CP3_BallisticController*				GetFreeController(CP3_BallisticController* prev = 0);
	virtual void							FrameUpdatePreEntityThink();
	static CP3_BallisticControllerCache&	Instance();
private:
	CUtlVector<CP3_BallisticController>		m_Controllers;
};

#endif