//------------------------------------------------------------------------------
// author  : Пятышев Иван, 2003, 2006
// desc    : 
//------------------------------------------------------------------------------

#ifndef P3_CORE_TEMPLATES_H
#define P3_CORE_TEMPLATES_H

namespace core
{

//------------------------------------------------------------------------------
// Помните, что Get() вернет разные значения в client.dll и server.dll

template <class T>
class TSingleton
{
public:
	TSingleton ()
	{
		// singleton должен быть только один
		assert(ms_pInstance == 0);
		ms_pInstance = static_cast<T*>(this);
	}
	~TSingleton() {ms_pInstance = 0;}
	static T* Get() {return ms_pInstance;}

private:
	static T* ms_pInstance;
};

template <class T>
T* TSingleton<T>::ms_pInstance = 0;

//------------------------------------------------------------------------------

}

#endif
