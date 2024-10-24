#ifndef __KERN_MM_BUDDY_SYSTEM_PMM_H__
#define  __KERN_MM_BUDDY_SYSTEM_PMM_H__

#include <pmm.h>

//将物理地址转换为内核虚拟地址的宏定义
#ifndef KADDR
#define KADDR(addr) ((void *)((uintptr_t)(addr) + PHYSICAL_MEMORY_OFFSET))
#endif

extern const struct pmm_manager BUDDY_SYSTEM_pmm_manager;

#endif /* ! __KERN_MM_BUDDY_SYSTEM_PMM_H__ */

