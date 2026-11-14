#pragma once

#include <cstddef>
#include <cstdint>


template<typename T>
class CUtlMemory
{
public:

	T* base;
	uint32_t alloc;
	uint32_t growSize;
};

template<typename T>
class CUtlVector
{
public:

	CUtlMemory<T> memory;
	uint32_t size;

	constexpr T* at(uint32_t index)
	{
		if (index >= size)
		{
			return nullptr;
		}

		return &memory.base[index];
	}
};

class CUtlBuffer
{
public:

	CUtlMemory<uint8_t> mem;	//0x0
	int32_t get;				//0x10 (x86_64) / 0xC (x86)
	int32_t put;				//0x14 (x86_64) / 0x10 (x86)
	int32_t offset;				//0x18 (x86_64) / 0x14 (x86)
	uint32_t flags;				//0x1C (x86_64) / 0x18 (x86)
#if defined(__x86_64__)
	uint8_t __pad0x20[0x20];	//0x20
#else
	uint8_t __pad0x1B[0x8];		//0x1B
#endif

	constexpr bool hasBytes(const std::size_t count) const
	{
		return mem.base && put >= 0 &&
			static_cast<std::size_t>(put) >= count;
	}
}; //0x40 (x86_64) / 0x24 (x86)
