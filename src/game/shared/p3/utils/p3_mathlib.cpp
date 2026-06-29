#include "cbase.h"
#include "p3_mathlib.h"


/**
 * Функция вычисляет начальную скорость прыжка
 * из точки from в точку to. При этом гарантируется,
 * что модуль скорости будет минимален.
 * Возможные препятствия на пути траектории прыжка не
 * учитываются.
 * Третьим параметром получает значение ускорения свободного падения.
 */
Vector CalcJumpVelNoObstacles(const Vector& from, const Vector& to, float G)
{
	Vector2D from2d(from.AsVector2D());
	Vector2D to2d(to.AsVector2D());
	float x1, y0, y1;
	float a, b, c;
	float vx2d, vy2d;
	Vector vel = to - from;

	x1 = (from2d - to2d).Length();
	y0 = from.z;
	y1 = to.z;

	a = 2.0f * (y0 - y1);
	b = 2.0f * x1;
	c = G * x1 * x1;

	Assert(b != 0);
	if (b == 0)
	{
		return Vector();
	}

	float tmp = c * c / (a * a + b * b);
	vx2d = ::pow(tmp, 0.25f);
	vy2d = - (a * vx2d + c / vx2d) / b;

	vel.z = 0;
	vel.NormalizeInPlace();
	vel *= vx2d;
	vel.z = vy2d;
	return vel;
}
