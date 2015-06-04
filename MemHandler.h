#if !defined(MEMHANDLER_H)
#define MEMHANDLER_H

#if !defined(stdint)
#include "stdint.h"
#endif

#if !defined(cmath)
#include <cmath>
#endif

#if !defined(Assert)
#define Assert(X) if(!(X)){(*(int *)0 = 0;}
#endif


/*
///////////////////////////////////EXAMPLE USAGE OF MACROS////////////////////////////////////
Struct_t TheStruct = ALLOCATEMEM(Struct_t, Manager);                       <-Usage
(Struct_t *) Allocate(sizeof(Struct_t *), Manager);                        <-Macro Expansion

FreeMem(TheStruct, Manager)                                                <-Usage
Free(sizeof(TheStruct), (void *) (&TheStruct), Manager);                   <-Macro Expansion
/////////////////////////////////////////////////////////////////////////////////////////////
*/

#define ALLOCATEMEM(Type_t,Manager) (Type_t *) Allocate(sizeof(Type_t), Manager);
#define FREEMEM(Struct, Manager) FreeMemory Free(sizeof(Struct), (void * ) (&Struct), Manager);

#define MANAGER_SIZE_DEFAULT (1024*64)
#define INITIAL_ALLOCATION_SIZE (512*1024*1024) // half a gig by default let's say

struct FreedMemoryBlock
{
	uint8_t * MemoryLocation;
	uint32_t SizeOfBlock;
};

struct ManagerHeader
{
	int FreedMemorySize;
	int FreedMemorySlots;
	int LastFreedIndex;

	size_t TotalAllocationSize;
	size_t InitialAllocationSize;
	size_t MaxAllocationSize;
	size_t RemainingUnallocated;
	size_t ManagerSize;

	uint8_t * NextAddress;
};

struct Memory_Manager
{
	ManagerHeader ManagerInfo;
	FreedMemoryBlock * FreedMemory;
	uint8_t * Memory;
};

//Something to note: by default manager cannot grow in size!! Please remember that if you do want to have a dynamic allocator
//you need to specify the the details of your allocation.
static Memory_Manager *
InitializeMemoryManager(uint64_t ManagerSize = MANAGER_SIZE_DEFAULT, 
						uint64_t InitialSize = INITIAL_ALLOCATION_SIZE, 
						uint64_t MaxSize = INITIAL_ALLOCATION_SIZE)
{
	Memory_Manager * Manager = (Memory_Manager *)VirtualAlloc(0,InitialSize,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);

	Manager->ManagerInfo.ManagerSize = ManagerSize;
	Manager->ManagerInfo.TotalAllocationSize = InitialSize;
	Manager->ManagerInfo.InitialAllocationSize = InitialSize;
	Manager->ManagerInfo.MaxAllocationSize = MaxSize;
	Manager->ManagerInfo.RemainingUnallocated = InitialSize - ManagerSize;

	Manager->ManagerInfo.FreedMemorySize = 0;
	Manager->ManagerInfo.LastFreedIndex = 0;
	Manager->ManagerInfo.FreedMemorySlots = (ManagerSize - sizeof(ManagerHeader)) / sizeof(FreedMemoryBlock);
	Manager->FreedMemory = (FreedMemoryBlock *)(Manager + sizeof(ManagerHeader));

	Manager->Memory = (uint8_t *)Manager + sizeof(Memory_Manager);
	Manager->ManagerInfo.NextAddress = Manager->Memory;

	return Manager;
}

//if a new freed memory block is added it needs to be inserted into the queue in the correct
//location. This utility function does that.
static void
InsertFreedMemory(Memory_Manager * Manager)
{
	FreedMemoryBlock FirstBlock = Manager->FreedMemory[Manager->ManagerInfo.LastFreedIndex];
	uint32_t BlockToCheck = Manager->ManagerInfo.LastFreedIndex;

	FreedMemoryBlock TempBlock = {};
	while(BlockToCheck != 0)
	{
		if(Manager->FreedMemory[--BlockToCheck].SizeOfBlock > FirstBlock.SizeOfBlock)
		{
			TempBlock = Manager->FreedMemory[BlockToCheck+1]; 
			Manager->FreedMemory[BlockToCheck+1] = Manager->FreedMemory[BlockToCheck];
			Manager->FreedMemory[BlockToCheck] = TempBlock;
		}
		else
		{
			break;
		}
	}
}

//if allocating from an already-existing freedblock, need to resort it's location in 
//the queue to find the the new correct location.
static void
ReSortFreedMemory(Memory_Manager * Manager, uint32_t BlockToReSortIndex)
{
	if(BlockToReSortIndex != 0)
	{
		uint32_t BlockToReSortSize = Manager->FreedMemory[BlockToReSortIndex].SizeOfBlock;

		uint32_t HighBlockIndex = BlockToReSortIndex;
		FreedMemoryBlock HighBlockTemp = {};

		//blocktoresort is always high at the top of loop, if loop fails, blocktoresort is in correct place and can exit
		while(Manager->FreedMemory[HighBlockIndex-1].SizeOfBlock > BlockToReSortSize)
		{
			HighBlockTemp = Manager->FreedMemory[HighBlockIndex--];
			Manager->FreedMemory[HighBlockIndex+1] = Manager->FreedMemory[HighBlockIndex];
			Manager->FreedMemory[HighBlockIndex] = HighBlockTemp;
		}
	}
}

//if a freed block is entirely removed from the queue, every element above the freed block needs to be moved down one index
static void
RemoveFromFreedMemory(Memory_Manager * Manager, uint32_t BlockToRemoveIndex)
{
	uint32_t EmptyIndex = BlockToRemoveIndex;

	while(EmptyIndex <= Manager->ManagerInfo.LastFreedIndex)
	{
		Manager->FreedMemory[EmptyIndex] = Manager->FreedMemory[EmptyIndex+1];
		++EmptyIndex;
	}
	Manager->FreedMemory[--Manager->ManagerInfo.LastFreedIndex] = {};
}

static size_t
Grow(Memory_Manager * Manager, size_t AllocSize)
{
	uint8_t * FinalAddressAllocated = (uint8_t *)Manager + Manager->ManagerInfo.InitialAllocationSize;
	
	size_t NewAllocSize = AllocSize;;
	size_t CurrentAllocationSize = Manager->ManagerInfo.InitialAllocationSize;
	size_t MaxAllocationSize = Manager->ManagerInfo.MaxAllocationSize;

	//If All allocations done by Memhandler this will always be safe. If not, It is recommended that you specify an
	//virtual address in the initial allocation that is guaranteed to be far enough away from all other allocations
	//that you do not have to worry about grow() impinging on a previous allocation.
	
	//Dont need to store the new address because we know it is contiguous with the previously allocated virtual 
	//memory block, we just need to reserve the virtual address space for the program
	void * ScratchAddress = (uint8_t *)VirtualAlloc((void *)FinalAddressAllocated, NewAllocSize, MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	Manager->ManagerInfo.TotalAllocationSize += NewAllocSize;
	Manager->ManagerInfo.RemainingUnallocated = NewAllocSize;
	return NewAllocSize;
}

static void * 
Allocate(size_t AllocSize, Memory_Manager * Manager)
{
	void * ReturnAddress = 0;

	//only check freed memory blocks if the entirety of the virtual allocation has been used
	//and the allocation size is at the maximum allowed for the app.
	if(Manager->ManagerInfo.RemainingUnallocated < AllocSize &&
	  (Manager->ManagerInfo.TotalAllocationSize - Manager->ManagerInfo.MaxAllocationSize) < AllocSize)
	{
		for(uint32_t FreeBlock = 0;
			FreeBlock < Manager->ManagerInfo.LastFreedIndex;
			++FreeBlock)
		{
			if(Manager->FreedMemory[FreeBlock].SizeOfBlock > AllocSize)
			{
				ReturnAddress = (void *)Manager->FreedMemory[FreeBlock].MemoryLocation;
				Manager->FreedMemory[FreeBlock].SizeOfBlock-=AllocSize;
				Manager->ManagerInfo.FreedMemorySize +=AllocSize;

				if(Manager->FreedMemory[FreeBlock].SizeOfBlock == 0)
				{
					RemoveFromFreedMemory(Manager,FreeBlock);
				}
				else
				{
					ReSortFreedMemory(Manager,FreeBlock);
				}
			}
		}
		
		Assert(ReturnAddress);
		return ReturnAddress;
	}
	else
	{
		if(Manager->ManagerInfo.RemainingUnallocated < AllocSize)
		{
			size_t AddedSize = Grow(Manager,AllocSize - Manager->ManagerInfo.RemainingUnallocated);
			Assert(AddedSize + Manager->ManagerInfo.RemainingUnallocated >= AllocSize);

			ReturnAddress = (void *)Manager->ManagerInfo.NextAddress;

			Manager->ManagerInfo.NextAddress = (uint8_t*)ReturnAddress + AllocSize;
			Manager->ManagerInfo.RemainingUnallocated -= AllocSize;
		}
		else
		{
			ReturnAddress = (void *)Manager->ManagerInfo.NextAddress;
			Manager->ManagerInfo.NextAddress += AllocSize;
			Manager->ManagerInfo.RemainingUnallocated -= AllocSize;
		}
	}

	Assert(ReturnAddress);
	return ReturnAddress; 
}

static void 
Free(size_t FreeSize, void * MemLocation, Memory_Manager * Manager)
{		
	Manager->ManagerInfo.FreedMemorySize += FreeSize;		
	Manager->FreedMemory[Manager->ManagerInfo.LastFreedIndex].MemoryLocation = (uint8_t *)MemLocation;
	Manager->FreedMemory[Manager->ManagerInfo.LastFreedIndex].SizeOfBlock = FreeSize;
	
	InsertFreedMemory(Manager);
	++Manager->ManagerInfo.LastFreedIndex;
}

#undef MANAGER_SIZE_DEFAULT 
#undef INITIAL_ALLOCATION_SIZE 

#endif
