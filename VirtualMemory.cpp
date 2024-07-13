//
// Created by iritv on 7/13/2024.
//
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "MemoryConstants.h"

#define ROOT_FRAME 0
#define SUCCESS_RET_VAL 1
#define FAILURE_RET_VAL 0

/*****************************************************************************
*                          Binary Calculations                               *
*****************************************************************************/

uint64_t getPageNumber (uint64_t virtualAddress)
{
  return virtualAddress >> OFFSET_WIDTH;
}

uint64_t getOffset (uint64_t virtualAddress)
{
  uint64_t offset_mask = (1 << OFFSET_WIDTH) - 1;
  return virtualAddress & offset_mask;
}

uint64_t getPageIndex (uint64_t page_number, uint64_t depth_level)
{
  uint64_t offset_mask = (1 << OFFSET_WIDTH) - 1;
  return (page_number >> (OFFSET_WIDTH * (TABLES_DEPTH - depth_level)))
         & offset_mask;
}

/*****************************************************************************
*                               Priority 1                                  *
*****************************************************************************/
bool isFrameEmpty (word_t frame_index)
{
  for (uint64_t row = 0; row < PAGE_SIZE; row++)
  {
    word_t value;
    PMread (frame_index * PAGE_SIZE + row, &value);
    if (value != 0)
    {
      return false;
    }
  }
  return true;
}

word_t getEmptyFrameIndex ()
{
  for (word_t frame_index = 1; frame_index < NUM_FRAMES; frame_index++)
  {
    if (isFrameEmpty (frame_index))
    {
      return frame_index;
    }
  }
  return -1;
}

/*****************************************************************************
*                               Priority 2                                   *
*****************************************************************************/

word_t getMaxFrameIndex (word_t curr_frame_index, uint64_t depth_level)
{
  word_t max_frame_index = curr_frame_index;
  //Base case
  if (curr_frame_index == TABLES_DEPTH)
  {
    return max_frame_index;
  }
  for (uint64_t row = 0; row < PAGE_SIZE; row++)
  {
    word_t next_frame_pointer;
    //Get the pointer to the next frame
    PMread (curr_frame_index * PAGE_SIZE + row, &next_frame_pointer);
    if (next_frame_pointer != 0)
    {
      //Call getMaxFrameIndex on next_frame_pointer
      word_t max_sub_frame_index = getMaxFrameIndex (next_frame_pointer,
                                                     depth_level + 1);
      if (max_sub_frame_index > max_frame_index)
      {
        max_frame_index = max_sub_frame_index;
      }
    }
  }
  return max_frame_index;
}

/*****************************************************************************
*                               Priority 3                                   *
*****************************************************************************/

uint64_t calculateCyclicalDistance (word_t swap_in_page, word_t page)
{
  uint64_t distance;
  if (swap_in_page > page)
  {
    distance = swap_in_page - page;
  }
  else
  {
    distance = page - swap_in_page;
  }
  return (NUM_PAGES - distance) < distance ? NUM_PAGES - distance : distance;
}

word_t getSwapFrameIndex (uint64_t swap_in_page)
{
//  word_t swap_out_page;
//  for (word_t frame_index = 1; frame_index < NUM_FRAMES; frame_index++)
//  {
//    uint64_t distance = calculateCyclicalDistance (swap_in_page, frame_index);
//  }

}

/*****************************************************************************
*                            Page Fault Handler                              *
*****************************************************************************/

void removeTableReference ();

word_t handlePageFault (word_t current_frame, uint64_t depth_level, uint64_t
page_number)
{
  //Priority 1
  word_t empty_frame_index = getEmptyFrameIndex ();
  if (empty_frame_index != -1)
  {
    //TODO Deal with dependencies and return
  }
  //Priority 2
  word_t max_frame_index = getMaxFrameIndex (current_frame, depth_level);
  if (max_frame_index + 1 < NUM_FRAMES)
  {
    //TODO Deal with dependencies and return
  }
  //Priority 3
  getSwapFrameIndex (page_number);
}


/*****************************************************************************
*                     Translate to Physical Address                          *
*****************************************************************************/

void translateVirtualAddress (uint64_t virtualAddress, uint64_t &
physical_address)
{
  uint64_t page_number = getPageNumber (virtualAddress);
  uint64_t curr_frame = ROOT_FRAME;
  for (int level = 0; level < TABLES_DEPTH; level++)
  {
    uint64_t page_index = getPageIndex (page_number, level);
    word_t next_frame = 0;
    PMread (curr_frame * PAGE_SIZE + page_index, &next_frame);
    if (next_frame == 0)
    {
      //TODO DEAL with Page fault
      next_frame = handlePageFault (next_frame, level, page_number);
    }
    curr_frame = next_frame;
  }
  uint64_t offset = getOffset (virtualAddress);
  physical_address = curr_frame * PAGE_SIZE + offset;
}

/*****************************************************************************
*                                  API                                       *
*****************************************************************************/

void VMinitialize ()
{
  //Initialize Frame 0 with rows equal to 0
  for (uint64_t row = 0; row < PAGE_SIZE; row++)
  {
    PMwrite (row, 0);
  }
}

int VMread (uint64_t virtualAddress, word_t *value)
{
  if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
  {
    return FAILURE_RET_VAL;
  }
  uint64_t physical_address;
  translateVirtualAddress (virtualAddress, physical_address);
  PMread (physical_address, value);
  return SUCCESS_RET_VAL;
}

int VMwrite (uint64_t virtualAddress, word_t value)
{
  if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
  {
    return FAILURE_RET_VAL;
  }
  uint64_t physical_address;
  translateVirtualAddress (virtualAddress, physical_address);
  PMwrite (physical_address, value);
  return SUCCESS_RET_VAL;
}
