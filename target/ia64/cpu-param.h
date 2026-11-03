#ifndef IA64_CPU_PARAM_H
#define IA64_CPU_PARAM_H

#define TARGET_PAGE_BITS 12
#ifdef CONFIG_USER_ONLY
/* The folded region windows of user-vaddr.h. */
#define TARGET_VIRT_ADDR_SPACE_BITS 43
#else
#define TARGET_VIRT_ADDR_SPACE_BITS 64
#endif

#endif
