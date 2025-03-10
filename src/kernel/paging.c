#include"paging.h"



// Static function to set a bit in the frames bitset
static void set_frame(uint32_t frame_addr)
{
   uint32_t frame = frame_addr/0x1000;
   uint32_t idx = INDEX_FROM_BIT(frame);
   uint32_t off = OFFSET_FROM_BIT(frame);
   frames[idx] |= (0x1 << off);
}

// Static function to clear a bit in the frames bitset
static void clear_frame(uint32_t frame_addr)
{
   uint32_t frame = frame_addr/0x1000;
   uint32_t idx = INDEX_FROM_BIT(frame);
   uint32_t off = OFFSET_FROM_BIT(frame);
   frames[idx] &= ~(0x1 << off);
}

// Static function to test if a bit is set.
static uint32_t test_frame(uint32_t frame_addr)
{
   uint32_t frame = frame_addr/0x1000;
   uint32_t idx = INDEX_FROM_BIT(frame);
   uint32_t off = OFFSET_FROM_BIT(frame);
   return (frames[idx] & (0x1 << off));
}

// Static function to find the first free frame.
static uint32_t first_frame()
{
   uint32_t i, j;
   for (i = 0; i < INDEX_FROM_BIT(nframes); i++)
   {
       if (frames[i] != 0xFFFFFFFF) // nothing free, exit early.
       {
           // at least one bit is free here.
           for (j = 0; j < 32; j++)
           {
               uint32_t toTest = 0x1 << j;
               if ( !(frames[i]&toTest) )
               {
                   return i*4*8+j;
               }
           }
       }
   }
   return 0; // Nothing Found
}

// Function to allocate a frame.
void alloc_frame(page_t *page, int is_kernel, int is_writeable)
{
   if (page->frame != 0)
   {
       kprint("already allocated");
       return; // Frame was already allocated, return straight away.
   }
   else
   {
       uint32_t idx = first_frame(); // idx is now the index of the first free frame.
       if (idx == (uint32_t)-1)
       {
           // kprint is just a macro that prints a message to the screen.
           kprint("No free frames!");
       }
       set_frame(idx*0x1000); // this frame is now ours!
       page->present = 1; // Mark it as present.
       page->rw = (is_writeable)?1:0; // Should the page be writeable?
       page->user = (is_kernel)?0:1; // Should the page be user-mode?
       page->frame = idx;
   }
}

// Function to allocate a frame.
void alloc_particular_frame(page_t *page, int is_kernel, int is_writeable,uint32_t idx)
{
   if (page->frame != 0)
   {
       return; // Frame was already allocated, return straight away.
   }
   else
   {
       //idx is now the index of the first free frame.
       if (test_frame(idx*0x1000)==1)
       {
           // kprint is just a macro that prints a message to the screen.
           kprint("Wrong Address");
       }
       set_frame(idx*0x1000); // this frame is now ours!
       page->present = 1; // Mark it as present.
       page->rw = (is_writeable)?1:0; // Should the page be writeable?
       page->user = (is_kernel)?0:1; // Should the page be user-mode?
       page->frame = idx;
   }
}

// Function to deallocate a frame.
void free_frame(page_t *page)
{
   uint32_t frame;
   if (!(frame=page->frame))
   {
       return; // The given page didn't actually have an allocated frame!
   }
   else
   {
       clear_frame(frame); // Frame is now free again.
       page->frame = 0x0; // Page now doesn't have a frame.
   }
}



void switch_page_directory(page_directory_t *dir)
{
   current_directory = dir;
   asm volatile("mov %0, %%cr3":: "r"(dir->physicalAddr)); // ******** MODIFIED *********
   uint32_t cr0;
   asm volatile("mov %%cr0, %0": "=r"(cr0));
   cr0 |= 0x80000000; // Enable paging!
   asm volatile("mov %0, %%cr0":: "r"(cr0));
}

page_t *get_page(uint32_t address, int make, page_directory_t *dir)
{
   // Turn the address into an index.
   address /= 0x1000;
   // Find the page table containing this address.
   uint32_t table_idx = address / 1024;
   if (dir->tables[table_idx]) // If this table is already assigned
   {
       return &dir->tables[table_idx]->pages[address%1024];
   }
   else if(make)
   {
       uint32_t tmp;
       dir->tables[table_idx] = (page_table_t*) kmalloc_ap(sizeof(page_table_t), &tmp);
       
       memset(dir->tables[table_idx], 0, 0x1000);
       dir->tablesPhysical[table_idx] = tmp | 0x7; // PRESENT, RW, US.
       return &dir->tables[table_idx]->pages[address%1024];
   }
   else
   {
       return 0;
   }
}
extern void copy_page_physical(uint32_t ,uint32_t );

static page_table_t *clone_table(page_table_t *src, uint32_t *physAddr)
{
   // Make a new page table, which is page aligned.
   page_table_t *table = (page_table_t*)kmalloc_ap(sizeof(page_table_t), physAddr);
   // Ensure that the new table is blank.
   memset(table, 0, sizeof(page_directory_t));

   // For every entry in the table...
   int i;
   for (i = 0; i < 1024; i++)
   {
     if (!src->pages[i].frame){
       continue;
     }
    // Get a new frame.
    alloc_frame(&table->pages[i], 0, 0);
    // Clone the flags from source to destination.
    if (src->pages[i].present) table->pages[i].present = 1;
    if (src->pages[i].rw)      table->pages[i].rw = 1;
    if (src->pages[i].user)    table->pages[i].user = 1;
    if (src->pages[i].accessed)table->pages[i].accessed = 1;
    if (src->pages[i].dirty)   table->pages[i].dirty = 1;
    // Physically copy the data across. This function is in process.s.
    copy_page_physical(src->pages[i].frame*0x1000, table->pages[i].frame*0x1000);
   }
   return table;
}



page_directory_t *clone_directory(page_directory_t *src)
{
   uint32_t phys;
   // Make a new page directory and obtain its physical address.
   page_directory_t *dir = (page_directory_t*)kmalloc_ap(sizeof(page_directory_t), &phys);
   // Ensure that it is blank.
   memset(dir, 0, sizeof(page_directory_t));
   // Get the offset of tablesPhysical from the start of the page_directory_t structure.
   uint32_t offset = (uint32_t)dir->tablesPhysical - (uint32_t)dir;

   // Then the physical address of dir->tablesPhysical is:
   dir->physicalAddr = phys + offset;

    int i;
    for (i = 0; i < 1024; i++)
    {
        if (!src->tables[i])  {
            continue;
        }

        if (kernel_directory->tables[i] == src->tables[i])
        {
           // It's in the kernel, so just use the same pointer.
           dir->tables[i] = src->tables[i];
           dir->tablesPhysical[i] = src->tablesPhysical[i];
        }
        else
        {
           // Copy the table.
           uint32_t phys;
           dir->tables[i] = clone_table(src->tables[i], &phys);
           dir->tablesPhysical[i] = phys | 0x07;
        }
    }
    return dir;
}



void page_fault(registers_t regs)
{
    // A page fault has occurred.
    // The faulting address is stored in the CR2 register.
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));
    
    // The error code gives us details of what happened.
    int present = !(regs.err_code & 0x1); // Page not present
    int rw = regs.err_code & 0x2;         // Write operation
    int us = regs.err_code & 0x4;         // User-mode (set to 0 for kernel-mode)
    int reserved = regs.err_code & 0x8;   // Overwritten CPU-reserved bits of page entry
    int id = regs.err_code & 0x10;        // Caused by an instruction fetch?
    UNUSED(id);
    // Output an error message.
    kprint_at("Page fault! (", 0, 1);
    if (present) { kprint("not present "); }
    else{ kprint("present-protection "); }
    if (rw) { kprint("write "); }
    else{ kprint("read "); }
    if (us) { kprint("user-mode "); } // Print "user-mode" only if it's set
    if (reserved) { kprint("reserved "); }
    
    kprint(") at ");
    kprint_hex(faulting_address);
    kprint("\n");
    kprint(" instruction:");
    kprint_hex(regs.eip);
    kprint("\n");
    
    // Additional error handling or actions can be added here if needed.

    // Halt the CPU or perform other actions as necessary.
    asm("hlt");
}

