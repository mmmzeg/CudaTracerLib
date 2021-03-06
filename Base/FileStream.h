#pragma once

#include <iostream>
#include <fstream>

#include <Math/Vector.h>
#include <Math/float4x4.h>
#include <Math/Spectrum.h>
#include <Math/AABB.h>
#include "FixedString.h"
#include <Engine/Buffer_device.h>

namespace CudaTracerLib {

class IInStream
{
protected:
	unsigned long long m_uFileSize;
	IInStream()
		: m_uFileSize(0)
	{
		
	}
public:
	virtual ~IInStream()
	{

	}
	virtual void Read(void* a_Out, size_t a_Size) = 0;
	virtual size_t getPos() = 0;
	size_t getFileSize() const
	{
		return m_uFileSize;
	}
	bool eof(){ return getPos() == getFileSize(); }
	virtual void Move(int off) = 0;
	virtual void Close() = 0;
	template<typename T> bool get(T& c)
	{
		if (getPos() + sizeof(T) <= getFileSize())
		{
			Read(&c, sizeof(T));
			return true;
		}
		else return false;
	}
	CTL_EXPORT bool ReadTo(std::string& str, char end);
	bool getline(std::string& str)
	{
		return ReadTo(str, '\n');
	}
	std::string getline()
	{
		std::string s;
		getline(s);
		return s;
	}
	CTL_EXPORT unsigned char* ReadToEnd();
	template<typename T> T* ReadToEnd()
	{
		return (T*)ReadToEnd();
	}
	template<typename T> void Read(T* a_Data, size_t a_Size)
	{
		Read((void*)a_Data, a_Size);
	}
	template<typename T> void Read(T& a)
	{
		Read((char*)&a, sizeof(T));
	}
	virtual const std::string& getFilePath() const = 0;
	template<int N> void Read(FixedString<N>& str)
	{
		Read((char*)&str, sizeof(FixedString<N>));
	}
	template<typename T, int N, bool b, unsigned char c> void Read(FixedSizeArray<T, N, b, c>& arr)
	{
		size_t l;
		*this >> l;
		arr.resize(l);
		for (unsigned int i = 0; i < l; i++)
			*this >> arr(i);
	}
public:
#define DCL_IN(TYPE) \
	CTL_EXPORT IInStream& operator>>(TYPE& rhs) \
	{ \
		Read(rhs); \
		return *this; \
	}
	DCL_IN(signed char)
	DCL_IN(short int)
	DCL_IN(int)
	DCL_IN(long int)
	DCL_IN(long long)
	DCL_IN(unsigned char)
	DCL_IN(unsigned short)
	DCL_IN(unsigned int)
	DCL_IN(unsigned long int)
	DCL_IN(unsigned long long)
	DCL_IN(float)
	DCL_IN(double)
	DCL_IN(Vec2i)
	DCL_IN(Vec3i)
	DCL_IN(Vec4i)
	DCL_IN(Vec2f)
	DCL_IN(Vec3f)
	DCL_IN(Vec4f)
	DCL_IN(Spectrum)
	DCL_IN(AABB)
	DCL_IN(Ray)
	DCL_IN(float4x4)
#undef DCL_IN

	template<typename VEC> IInStream& operator>>(NormalizedT<VEC>& rhs)
	{
		return *this >> (VEC)rhs;
	}

	template<typename H, typename D> IInStream& operator>>(BufferReference<H, D>& rhs)
	{
		Read(rhs(0), rhs.getHostSize());
		rhs.Invalidate();
		return *this;
	}
};

class FileInputStream : public IInStream
{
private:
	size_t numBytesRead;
	void* H;
	std::string path;
public:
	CTL_EXPORT explicit FileInputStream(const std::string& a_Name);
	virtual ~FileInputStream()
	{
		Close();
	}
	CTL_EXPORT virtual void Close();
	virtual size_t getPos()
	{
		return numBytesRead;
	}
	CTL_EXPORT virtual void Read(void* a_Data, size_t a_Size);
	CTL_EXPORT void Move(int off);
	CTL_EXPORT virtual const std::string& getFilePath() const
	{
		return path;
	}
};

class MemInputStream : public IInStream
{
private:
	size_t numBytesRead;
	const unsigned char* buf;
	std::string path;
public:
	CTL_EXPORT MemInputStream(const unsigned char* buf, size_t length, bool canKeep = false);
	CTL_EXPORT explicit MemInputStream(FileInputStream& in);
	CTL_EXPORT explicit MemInputStream(const std::string& a_Name);
	~MemInputStream()
	{
		Close();
	}
	virtual void Close()
	{
		if (buf)
		{
			free((void*)buf);
			buf = 0;
		}
		buf = 0;
	}
	virtual size_t getPos()
	{
		return numBytesRead;
	}
	CTL_EXPORT virtual void Read(void* a_Data, size_t a_Size);
	void Move(int off)
	{
		numBytesRead += off;
	}
	virtual const std::string& getFilePath() const
	{
		return path;
	}
};

CTL_EXPORT IInStream* OpenFile(const std::string& filename);

class FileOutputStream
{
private:
	size_t numBytesWrote;
	void* H;
	CTL_EXPORT void _Write(const void* data, size_t size);
public:
	CTL_EXPORT explicit FileOutputStream(const std::string& a_Name);
	virtual ~FileOutputStream()
	{
		Close();
	}
	CTL_EXPORT void Close();
	size_t GetNumBytesWritten() const
	{
		return numBytesWrote;
	}
	template<typename T> void Write(T* a_Data, size_t a_Size)
	{
		_Write(a_Data, a_Size);
	}
	template<typename T> void Write(const T& a_Data)
	{
		_Write(&a_Data, sizeof(T));
	}
	template<int N> void Write(const FixedString<N>& str)
	{
		Write((char*)&str, sizeof(FixedString<N>));
	}
	template<typename T, int N, bool b, unsigned char c> void Write(const FixedSizeArray<T, N, b, c>& arr)
	{
		*this << arr.size();
		for (size_t i = 0; i < arr.size(); i++)
			*this << arr(i);
	}
#define DCL_OUT(TYPE) \
	CTL_EXPORT FileOutputStream& operator<<(const TYPE& rhs) \
		{ \
		Write(rhs); \
		return *this; \
		}
	DCL_OUT(signed char)
	DCL_OUT(short int)
	DCL_OUT(int)
	DCL_OUT(long int)
	DCL_OUT(long long)
	DCL_OUT(unsigned char)
	DCL_OUT(unsigned short)
	DCL_OUT(unsigned int)
	DCL_OUT(unsigned long int)
	DCL_OUT(unsigned long long)
	DCL_OUT(float)
	DCL_OUT(double)
	DCL_OUT(Vec2i)
	DCL_OUT(Vec3i)
	DCL_OUT(Vec4i)
	DCL_OUT(Vec2f)
	DCL_OUT(Vec3f)
	DCL_OUT(Vec4f)
	DCL_OUT(Spectrum)
	DCL_OUT(AABB)
	DCL_OUT(Ray)
	DCL_OUT(float4x4)
#undef DCL_OUT

	template<typename H, typename D> FileOutputStream& operator<<(const BufferReference<H, D>& rhs)
	{
		_Write(rhs(0), rhs.getHostSize());
		return *this;
	}

	template<typename VEC> FileOutputStream& operator<<(const NormalizedT<VEC>& rhs)
	{
		return *this << (VEC)rhs;
	}
};

}
