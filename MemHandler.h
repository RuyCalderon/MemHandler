#if !defined(MEMHANDLER_H)
#define MEMHANDLER_H

#if !defined(stdint)
#include "stdint.h"
#endif

#if !defined(Assert)
#define Assert(X) if(!(X)){(*(int *)0 = 0;}
#endif

#define MANAGER_SIZE_DEFAULT (1024*64)
#define INITIAL_ALLOCATION_SIZE (512*1024*1024) // one gig to start let's say


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

static Memory_Manager *
InitializeMemoryManager(uint64_t ManagerSize = MANAGER_SIZE_DEFAULT, 
					   uint64_t InitialSize = INITIAL_ALLOCATION_SIZE, 
					   uint64_t MaxSize = INITIAL_ALLOCATION_SIZE)
{
	Memory_Manager * Manager = (Memory_Manager *)VirtualAlloc(0,InitialSize,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);

	Manager->ManagerInfo.ManagerSize = ManagerSize;
	Manager->ManagerInfo.FreedMemorySize = 0;
	Manager->ManagerInfo.LastFreedIndex = 0;
	Manager->ManagerInfo.TotalAllocationSize = InitialSize;
	Manager->ManagerInfo.RemainingUnallocated = InitialSize - ManagerSize;
	Manager->ManagerInfo.FreedMemorySlots = (ManagerSize - sizeof(ManagerHeader)) / sizeof(FreedMemoryBlock);
	Manager->FreedMemory = (FreedMemoryBlock *)(Manager + sizeof(ManagerHeader));

	Manager->Memory = (uint8_t *)Manager + sizeof(Memory_Manager);
	Manager->ManagerInfo.NextAddress = Manager->Memory;

	return Manager;
}

//if a new freed memory block is added it needs to be inserted into the queue in the correct
//location to make the eventual reallocation faster. There are better ways of doing this, this
//one is fine for now though
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

// [BLOCKTORESORT] smaller than it should be
// [BLOCKTOCHECK]  if(BlockToCheck is larger than BlockToResort) then BlockToResort goes in the place of BlockToCheck
// 				   if(BlockToCheck is smaller than BlockToResort) then BlockToResort is in the right place
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

static void * 
Allocate(size_t AllocSize, Memory_Manager * Manager)
{
	void * ReturnAddress = 0;

	if(Manager->ManagerInfo.RemainingUnallocated < AllocSize)
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
			//Grow
			Assert((uint8_t)"Ya Done Goofed" == 0);
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


/*
These are for later
static void
Grow()
{

}

static void 
Shrink()
{

}

*/

#undef MANAGER_SIZE_DEFAULT
#undef INITIAL_ALLOCATION_SIZE

#endif
