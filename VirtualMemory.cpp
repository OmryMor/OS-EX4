
#include "VirtualMemory.h"
#include "PhysicalMemory.h"

#define ROOT_FRAME 0
#define PAGE_FAULT 0
#define INITIAL_DEPTH_LEVEL 0
#define SUCCESS_RET_VAL 1
#define FAILURE_RET_VAL 0
#define NO_FRAME_FOUND (-1)

/*****************************************************************************
*                          Binary Calculations                               *
*****************************************************************************/

typedef enum
{
    PAGE_NUMBER,
    OFFSET,
    PAGE_INDEX
} BinaryOperation;

uint64_t calculateBits (uint64_t virtualAddress, BinaryOperation operation,
                        uint64_t depth_level = 0)
{
  uint64_t offset_mask = (1 << OFFSET_WIDTH) - 1;
  uint64_t bits = 0;
  switch (operation)
  {
    case PAGE_NUMBER:
      bits = virtualAddress >> OFFSET_WIDTH;
      break;
    case OFFSET:
      bits = virtualAddress & offset_mask;
      break;
    case PAGE_INDEX:
      uint64_t shift = OFFSET_WIDTH * (TABLES_DEPTH - depth_level);
      bits = (virtualAddress >> shift) & offset_mask;
      break;
  }
  return bits;
}

uint64_t getNextPage (uint64_t current_page, uint64_t current_row)
{
  return (current_page << OFFSET_WIDTH) + current_row;
}

/*****************************************************************************
*                            DFS Implementation                              *
*****************************************************************************/




/*****************************************************************************
*                               Priority 1                                  *
*****************************************************************************/

bool isFrameEmpty (word_t frame_index)
{
  for (uint64_t row = 0; row < PAGE_SIZE; row++)
  {
    word_t value;
    PMread ((uint64_t) (frame_index) * PAGE_SIZE + row, &value);
    if (value != PAGE_FAULT)
    {
      return false;
    }
  }
  return true;
}

word_t getEmptyFrame (word_t original_frame, word_t current_frame,
                      word_t parent_frame, uint64_t parent_row_index,
                      uint64_t depth_level)
{
  //Check if we got to the end of the tree
  if (depth_level == TABLES_DEPTH)
  {
    return NO_FRAME_FOUND;
  }
  //We do not want to return the original frame
  if (current_frame == original_frame)
  {
    return NO_FRAME_FOUND;
  }

  if (isFrameEmpty (current_frame))
  {
    if (current_frame == ROOT_FRAME)
    {
      return NO_FRAME_FOUND;
    }
    //remove table reference to current frame before returning
    PMwrite ((uint64_t) (parent_frame) * PAGE_SIZE + parent_row_index, 0);
    return current_frame;
  }
  for (uint64_t row = 0; row < PAGE_SIZE; row++)
  {
    word_t next_frame = 0;
    PMread ((uint64_t) (current_frame) * PAGE_SIZE + row, &next_frame);
    if (next_frame != PAGE_FAULT)
    {
      word_t candidate_empty_frame = getEmptyFrame (original_frame, next_frame,
                                                    current_frame,
                                                    row, depth_level + 1);
      if (candidate_empty_frame != NO_FRAME_FOUND)
      {
        return candidate_empty_frame;
      }
    }
  }
  return NO_FRAME_FOUND;
}

word_t searchForEmptyFrame (word_t original_frame)
{
  return getEmptyFrame (original_frame, ROOT_FRAME,
                        ROOT_FRAME, 0, INITIAL_DEPTH_LEVEL);
}

/*****************************************************************************
*                               Priority 2                                   *
*****************************************************************************/

word_t getMaxFrame (word_t curr_frame_index, uint64_t depth_level)
{
  word_t max_frame_index = curr_frame_index;
  //Base case
  if (depth_level == TABLES_DEPTH)
  {
    return max_frame_index;
  }
  for (uint64_t row = 0; row < PAGE_SIZE; row++)
  {
    word_t next_frame = 0;
    //Get the pointer to the next frame
    PMread (curr_frame_index * PAGE_SIZE + row, &next_frame);
    if (next_frame != PAGE_FAULT)
    {
      //Call getMaxFrame on next_frame
      word_t max_candidate = getMaxFrame (next_frame,
                                          depth_level + 1);
      if (max_candidate > max_frame_index)
      {
        max_frame_index = max_candidate;
      }
    }
  }
  return max_frame_index;
}

word_t searchForMaxFrame ()
{
  word_t max_frame_index = getMaxFrame (ROOT_FRAME, INITIAL_DEPTH_LEVEL);
  if (max_frame_index + 1 < NUM_FRAMES)
  {
    return max_frame_index + 1;
  }
  return NO_FRAME_FOUND;
}

/*****************************************************************************
*                               Priority 3                                   *
*****************************************************************************/

typedef struct
{
    word_t parent;
    uint64_t child_offset;
    uint64_t page;
    uint64_t distance;
} SwapFrameData;

uint64_t calculateCyclicalDistance (word_t swap_in_page, word_t page)
{
  uint64_t distance = (swap_in_page > page) ? (swap_in_page - page) : (page
                                                                       - swap_in_page);
  uint64_t cyclic = NUM_PAGES - distance;
  return (cyclic < distance) ? cyclic : distance;
}

SwapFrameData searchFrameToEvict (uint64_t swap_in_page, word_t current_frame,
                                  word_t parent_frame, uint64_t
                                  parent_row_index, uint64_t page, uint64_t
                                  depth_level)
{
  if (depth_level == TABLES_DEPTH)
  {
    uint64_t distance = calculateCyclicalDistance (swap_in_page, page);
    return {parent_frame, parent_row_index, page, distance};
  }
  SwapFrameData swap_out_parent = {};
  for (uint64_t row = 0; row < PAGE_SIZE; row++)
  {
    word_t next_frame = 0;
    uint64_t new_page = getNextPage (page, row);
    PMread (current_frame * PAGE_SIZE + row, &next_frame);
    if (next_frame != PAGE_FAULT)
    {
      SwapFrameData candidate = searchFrameToEvict (swap_in_page, next_frame,
                                                    current_frame, row, new_page,
                                                    depth_level + 1);
      if (candidate.distance > swap_out_parent.distance)
      {
        swap_out_parent = candidate;
      }
    }
  }
  return swap_out_parent;
}

word_t evictAndRemoveReference (SwapFrameData pair)
{
  word_t child = 0;
  //Find the evicted child
  PMread (pair.parent * PAGE_SIZE + pair.child_offset, &child);
  PMevict (child, pair.page);
  //Remove reference
  PMwrite (pair.parent * PAGE_SIZE + pair.child_offset, 0);
  return child;
}

word_t swapFrames(uint64_t swap_in_page){
  SwapFrameData parent_to_evict = searchFrameToEvict (swap_in_page,
                                                      ROOT_FRAME,
                                                      ROOT_FRAME, 0, 0, INITIAL_DEPTH_LEVEL);
  return evictAndRemoveReference (parent_to_evict);
}



/*****************************************************************************
*                            Page Fault Handler                              *
*****************************************************************************/

word_t handlePageFault (word_t current_frame, uint64_t page_number)
{
  //Priority 1
  word_t empty_frame_index = searchForEmptyFrame (current_frame);
  if (empty_frame_index != NO_FRAME_FOUND)
  {
    return empty_frame_index;
  }

  //Priority 2
  word_t max_frame_index = searchForMaxFrame ();
  if (max_frame_index != NO_FRAME_FOUND)
  {
    return max_frame_index;
  }

  //Priority 3
  return swapFrames (page_number);
}

/*****************************************************************************
*                     Translate to Physical Address                          *
*****************************************************************************/

void createNewTable (word_t frame, uint64_t depth_level)
{
  if (depth_level < TABLES_DEPTH - 1)
  {
    for (uint64_t row = 0; row < PAGE_SIZE; row++)
    {
      PMwrite ((uint64_t) (frame) * PAGE_SIZE + row, 0);
    }
  }
}

void translateVirtualAddress (uint64_t virtualAddress, uint64_t &
physical_address)
{
  uint64_t page_number = calculateBits (virtualAddress, PAGE_NUMBER);
  word_t curr_frame = ROOT_FRAME;
  for (uint64_t level = 0; level < TABLES_DEPTH; level++)
  {
    uint64_t page_index = calculateBits (virtualAddress, PAGE_INDEX, level);
    word_t next_frame = 0;
    PMread ((uint64_t) (curr_frame) * PAGE_SIZE + page_index, &next_frame);
    if (next_frame == PAGE_FAULT)
    {
      next_frame = handlePageFault (curr_frame, page_number);
      createNewTable (next_frame, level);
      PMwrite ((uint64_t) (curr_frame) * PAGE_SIZE + page_index, next_frame);
      if (level == TABLES_DEPTH - 1)
      {
        PMrestore (next_frame, page_number);
      }
    }
    curr_frame = next_frame;
  }
  uint64_t offset = calculateBits (virtualAddress, OFFSET);
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
  if (value == nullptr)
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
