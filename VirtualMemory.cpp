//
// Created by iritv on 7/13/2024.
//
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "MemoryConstants.h"

#define ROOT_FRAME 0
#define SUCCESS_RET_VAL 1
#define FAILURE_RET_VAL 0
#define NO_FRAME_FOUND -1
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

uint64_t getPageIndex (uint64_t virtualAddress, uint64_t depth_level) {
    uint64_t mask = (1 << OFFSET_WIDTH) - 1;
    uint64_t p = (virtualAddress >> (OFFSET_WIDTH * (TABLES_DEPTH -depth_level))) & mask;
    return p;
//  uint64_t offset_mask = (1 << OFFSET_WIDTH) - 1;
//  return (virtualAddress >> (OFFSET_WIDTH * (TABLES_DEPTH - depth_level)))
//         & offset_mask;
}

uint64_t getPageFromFrame(word_t frame_index, uint64_t row){
  return frame_index << OFFSET_WIDTH | row;
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

word_t getEmptyFrameIndex (word_t original_frame, word_t current_frame,
                           word_t parent_frame, uint64_t parent_row_index,
                           uint64_t depth_level)
{
  if (original_frame != current_frame && isFrameEmpty (current_frame))
  {
    if (current_frame != ROOT_FRAME)
    {
      //remove table reference to current frame before returning
      PMwrite (parent_frame * PAGE_SIZE + parent_row_index, 0);
      return current_frame;
    }
    else
    {
      return NO_FRAME_FOUND;
    }
  }

  if (depth_level == TABLES_DEPTH)
  {
    return NO_FRAME_FOUND;
  }
  for (uint64_t row = 0; row < PAGE_SIZE; row++)
  {
    word_t next_frame;
    PMread (current_frame * PAGE_SIZE + row, &next_frame);
    if (next_frame != 0)
    {
      parent_frame = current_frame;
      current_frame = next_frame;
      word_t empty_frame_index = getEmptyFrameIndex (original_frame, current_frame, parent_frame,
                                                     row, depth_level + 1);
      if (empty_frame_index != NO_FRAME_FOUND)
      {
        return empty_frame_index;
      }
    }
  }
  return NO_FRAME_FOUND;
}

/*****************************************************************************
*                               Priority 2                                   *
*****************************************************************************/

word_t getMaxFrameIndex (word_t curr_frame_index, uint64_t depth_level)
{
  word_t max_frame_index = curr_frame_index;
  //Base case
  if (depth_level == TABLES_DEPTH)
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

typedef struct{
    word_t parent;
    uint64_t child_offset;
    uint64_t page;
}ParentChildPair;


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

ParentChildPair getSwapFrame (uint64_t swap_in_page, word_t current_frame,
                              word_t parent_frame, uint64_t
                           parent_row_index, uint64_t page, uint64_t
                           depth_level, uint64_t &max_distance)
{
  if (depth_level == TABLES_DEPTH)
  {
    int distance = calculateCyclicalDistance (swap_in_page, page);
    if (distance > max_distance)
    {
      max_distance = distance;
      return {parent_frame, parent_row_index,page};
    }
  }
  ParentChildPair swap_out_parent = {parent_frame, parent_row_index,page};
  for (uint64_t row = 0; row < PAGE_SIZE; row++)
  {
    word_t next_frame;
    page = (page << OFFSET_WIDTH) + row;
    PMread (current_frame * PAGE_SIZE + row, &next_frame);
    if (next_frame != 0)
    {
      swap_out_parent = getSwapFrame (swap_in_page, next_frame, current_frame,
                                      row, page, depth_level + 1,
                                      max_distance);
    }
  }
  return swap_out_parent;
}


word_t removeRefernce(ParentChildPair pair){
  word_t child;
  PMread (pair.parent*PAGE_SIZE + pair.child_offset, &child);
  PMevict (child, pair.page);
  PMwrite (pair.parent*PAGE_SIZE + pair.child_offset, 0);
}

/*****************************************************************************
*                            Page Fault Handler                              *
*****************************************************************************/



word_t handlePageFault (word_t current_frame, uint64_t depth_level, uint64_t
page_number)
{
  //Priority 1
  word_t empty_frame_index = getEmptyFrameIndex (current_frame, ROOT_FRAME,
                                                 0, 0, 0);
  if (empty_frame_index != NO_FRAME_FOUND)
  {
    return empty_frame_index;
  }
  //Priority 2
  word_t max_frame_index = getMaxFrameIndex (ROOT_FRAME, 0);
  if (max_frame_index + 1 < NUM_FRAMES)
  {
    return max_frame_index + 1;
  }
  //Priority 3
  uint64_t max_distance = 0;
  ParentChildPair pair = getSwapFrame (page_number,
                                       ROOT_FRAME,
                                       ROOT_FRAME, 0,0,0,
                                       max_distance);

  word_t swap_out_frame = removeRefernce (pair);
  return swap_out_frame;
}

/*****************************************************************************
*                     Translate to Physical Address                          *
*****************************************************************************/

void createNewTable(word_t parent ,word_t frame, uint64_t index){
  for(uint64_t row = 0; row < PAGE_SIZE; row ++){
    PMwrite(frame * PAGE_SIZE + row, 0);
  }
  PMwrite (parent*PAGE_SIZE + index, frame);
}

void translateVirtualAddress (uint64_t virtualAddress, uint64_t &
physical_address)
{
  bool page_fault = false;
  uint64_t page_number = getPageNumber (virtualAddress);
  word_t curr_frame = ROOT_FRAME;
    for (uint64_t level = 0; level < TABLES_DEPTH; level++) {

        uint64_t page_index = getPageIndex(virtualAddress, level);
        word_t next_frame=1;
        PMread(curr_frame * PAGE_SIZE + page_index, &next_frame);
        if (next_frame == 0) {
            page_fault = true;
            next_frame = handlePageFault(curr_frame, level, page_number);
            if (level < TABLES_DEPTH - 1) {
                createNewTable(curr_frame, next_frame, page_index);
            }
        }
        curr_frame = next_frame;
    }
  if(page_fault){
    PMrestore (curr_frame, page_number);
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
