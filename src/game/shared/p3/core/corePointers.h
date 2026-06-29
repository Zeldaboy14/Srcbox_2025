//--------------------------------------------------------------------------------------------------
// Desc    : Объект с подсчетом ссылок, smart-указатели
// Author  : Пятышев Иван, 2006
//--------------------------------------------------------------------------------------------------

#ifndef P3_CORE_POINTERS_H
#define P3_CORE_POINTERS_H

namespace core
{

//--------------------------------------------------------------------------------------------------
// элемент связного списка, который образуют weak-указатели на один RefObject
struct SWeakData
{
	SWeakData() :
		next(0),
		prev(0),
		data(0)
	{}

	SWeakData*	next;
	SWeakData*	prev;
	void*		data;

	// удалить себя из текущего списка, связать ссылки next и prev соседей
	void RemoveSelf();

	// разорвать все связи списка, обнулить все data
	void ClearAll();
};

//--------------------------------------------------------------------------------------------------
// Объект с подсчетом ссылок

class CRefObject
{
public:
	CRefObject () :
		m_nCount (0),
		m_list(0)
	{}
	virtual ~CRefObject ();

	void AddRef () {++m_nCount;}
	void Release ();
	int  GetRefCount () const {return m_nCount;}

	void		AddWeakPointer(SWeakData* pNew);
	void		RemoveWeakPointer(SWeakData* p);
	SWeakData*	GetWeakList() const {return m_list;}

private:
	// начало связного списка weak-указателей
	SWeakData*	m_list;

	unsigned short	m_nCount;
};

//--------------------------------------------------------------------------------------------------
template <class T>
class TWeakPointer
{
public:

	// конструкторы
	TWeakPointer() {}

	TWeakPointer(const TWeakPointer& wp)
	{
		operator = (wp);
	}

	TWeakPointer(T* p) 
	{
		operator = (p);
	}

	template <class U>	TWeakPointer(const TWeakPointer<U>& wp) 
	{
		T* p = wp.get();
		operator = (p);
	}

	// деструктор
	~TWeakPointer()
	{
		operator = ((T*)0);
	}

	// операторы присваивания
	TWeakPointer& operator = (const TWeakPointer& wp) 
	{
		operator = (wp.get());
		return *this;
	}

	TWeakPointer& operator = (T* p)
	{
		if (get() == p)
			return *this;

		// удалить себя из текущего списка
		if (m_data.data != 0)
			get()->removeWeakPointer(&m_data);

		m_data.data = p;

		if (m_data.data != 0)
		{
			// вставить себя в новый список
			p->addWeakPointer(&m_data);
		}

		return *this;
	}

	// доступ
	T* Get() const 
	{
		return (T*)m_data.data;
	}

	T* operator ->() const 
	{
		return Get();
	}

	//
    operator bool() const {return Get() != 0;}
	bool operator!() const {return Get() == 0;}

	// операторы сравнения
	bool operator == (const TWeakPointer& wp) const { return Get() == wp.Get(); }
	bool operator != (const TWeakPointer& wp) const { return Get() != wp.Get(); }
	bool operator <  (const TWeakPointer& wp) const { return Get() <  wp.Get(); }
	bool operator <= (const TWeakPointer& wp) const { return Get() <= wp.Get(); }
	bool operator >  (const TWeakPointer& wp) const { return Get() >  wp.Get(); }
	bool operator >= (const TWeakPointer& wp) const { return Get() >= wp.Get(); }

private:
	// элемент связного списка
	SWeakData m_data;
};

//--------------------------------------------------------------------------------------------------

} // namespace core

#endif