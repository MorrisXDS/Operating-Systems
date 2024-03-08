#include "vms.h"

#include "mmu.h"
#include "pages.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#ifndef regular_copy
#define regular_copy 1
#endif

#ifndef copy_on_write
#define copy_on_write 0
#endif

int function_call_times = 0;

struct page_reference {
  void *old_pages[256];
  void *new_pages[256];
  int new_page_index;
  int old_page_index;
};

struct copy_free {
  uint64_t unique[128];
};

int type = -1;

struct page_reference page;

struct copy_free array;
int times[128] = {1};


int array_index = 0;
int used_pages = 0;


void copy_free_push(uint64_t *element) {
  for (int i = 0; i < array_index; i++) {
    if (array.unique[i] == vms_pte_get_ppn(element)) {
      times[i]++;
      return;
    }
  }
  array.unique[array_index] = vms_pte_get_ppn(element);
  times[array_index] = 1;
  array_index++;
}

int check_unqiueness(uint64_t *element) {
  for (int i = 0; i < array_index; i++) {
    if (array.unique[i] != vms_pte_get_ppn(element)) continue;
    
    if (times[i] == 1) return 1;
    
    times[i]--;
    return 0;
  }
  return -1;
}

void page_fault_handler(void *virtual_address, int level, void *page_table) {
  int index = page.new_page_index;
  if (type == regular_copy) {
    return;
  }

  int page_index = vms_get_page_index(vms_get_root_page_table());

  void *level_0 = vms_get_page_pointer(page_index + 2);

  uint64_t *entry;

  if (page_index != 0) {

    entry = vms_page_table_pte_entry(level_0, virtual_address, level);

    if (!check_unqiueness(entry)) {
      page.new_pages[(page.new_page_index)++] = vms_new_page();
      vms_pte_set_ppn(entry, vms_page_to_ppn(page.new_pages[index]));
    }

    if (!vms_pte_custom(entry))
      vms_pte_write_set(entry);

  } else {
    entry = vms_page_table_pte_entry(page_table, virtual_address, level);
    if (!check_unqiueness(entry)) {

      page.new_pages[(page.new_page_index)++] = vms_new_page();
      vms_pte_set_ppn(entry, vms_page_to_ppn(page.new_pages[index]));
    }

    if (!vms_pte_custom(entry))
      vms_pte_write_set(entry);
  }
}

void *vms_fork_copy() {
  type = regular_copy;
  used_pages = vms_get_used_pages();
  page.new_page_index = 0;
  page.old_page_index = 0;

  for (int i = 0; i < used_pages; i++) {
    page.new_pages[i] = vms_new_page();
    page.new_page_index++;
    page.old_pages[i] = vms_get_page_pointer(i);
    page.old_page_index++;
    memcpy(page.new_pages[i], page.old_pages[i], PAGE_SIZE);
  }

  for (int i = 0; i < 3; i++) {
    for (int page_entry_index = 0; page_entry_index < NUM_PTE_ENTRIES;
         page_entry_index++) {
      uint64_t *entry = vms_page_table_pte_entry_from_index(page.old_pages[i],
                                                            page_entry_index);

      if (vms_pte_valid(entry)) {
        int index = -1;

        uint64_t ppn = vms_pte_get_ppn(entry);
        for (int ppn_index = 0; ppn_index < used_pages; ppn_index++) {
          if (vms_page_to_ppn(page.old_pages[ppn_index]) == ppn) {
            index = ppn_index;
          }
        }

        uint64_t *new_entry = vms_page_table_pte_entry_from_index(
            page.new_pages[i], page_entry_index);
        vms_pte_set_ppn(new_entry, vms_page_to_ppn(page.new_pages[index]));
        vms_page_to_ppn(page.new_pages[index]);
      }
    }
  }

  return page.new_pages[0];
}

void *vms_fork_copy_on_write() {
  type = copy_on_write;
  used_pages = vms_get_used_pages();
  page.new_page_index = 0;
  page.old_page_index = 0;

  for (int i = 0; i < 3; i++) {
    page.new_pages[i] = vms_new_page();
    page.new_page_index++;
    page.old_pages[i] = vms_get_page_pointer(i);
    memcpy(page.new_pages[i], page.old_pages[i], PAGE_SIZE);
  }

  for (int i = 0; i < 2; i++) {
    for (int page_entry_index = 0; page_entry_index < NUM_PTE_ENTRIES;
         page_entry_index++) {
      uint64_t *entry = vms_page_table_pte_entry_from_index(page.old_pages[i],
                                                            page_entry_index);

      if (vms_pte_valid(entry)) {
        uint64_t *new_entry = vms_page_table_pte_entry_from_index(
            page.new_pages[i], page_entry_index);
        vms_pte_set_ppn(new_entry, vms_page_to_ppn(page.new_pages[i + 1]));
        vms_pte_valid_set(new_entry);
      }
    }
  }

  for (int page_entry_index = 0; page_entry_index < NUM_PTE_ENTRIES;
       page_entry_index++) {
    uint64_t *entry = vms_page_table_pte_entry_from_index(page.new_pages[2],
                                                          page_entry_index);
    uint64_t *old_entry = vms_page_table_pte_entry_from_index(page.old_pages[2],
                                                              page_entry_index);

    if (vms_pte_valid(entry)) {
      copy_free_push(entry);
      if (!vms_pte_write(entry) && function_call_times == 0) {
        vms_pte_custom_set(entry);
      }
      vms_pte_write_clear(entry);
    }

    if (vms_pte_valid(old_entry) && function_call_times == 0) {
      if (!vms_pte_write(old_entry)) {
        vms_pte_custom_set(old_entry);
      }
      vms_pte_write_clear(old_entry);
      copy_free_push(entry);
    }
  }

  function_call_times++;

  return page.new_pages[0];
}
