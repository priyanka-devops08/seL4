/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <api/syscall.h>
#include <config.h>
#include <machine/io.h>
#include <kernel/boot.h>
#include <kernel/cspace.h>
#include <kernel/thread.h>
#include <model/statedata.h>
#include <object/cnode.h>
#include <arch/api/invocation.h>
#include <arch/kernel/apic.h>
#include <arch/kernel/vspace.h>
#include <arch/linker.h>
#include <util.h>

#ifndef CONFIG_PAE_PAGING

/* setup initial boot page directory */

/* The boot pd is referenced by code that runs before paging, so
 * place it in PHYS_DATA */
pde_t _boot_pd[BIT(PD_INDEX_BITS)] ALIGN(BIT(PAGE_BITS)) VISIBLE PHYS_DATA;

BOOT_CODE
pde_t *get_boot_pd()
{
    return _boot_pd;
}

/* This function is duplicated from pde_pde_large_ptr_new, generated by the
 * bitfield tool in structures_gen.h. It is required by functions that need to
 * call it before the MMU is turned on. Any changes made to the bitfield
 * generation need to be replicated here.
 */
PHYS_CODE
static inline void
pde_pde_large_ptr_new_phys(pde_t *pde_ptr, uint32_t page_base_address,
                           uint32_t pat, uint32_t avl, uint32_t global, uint32_t dirty,
                           uint32_t accessed, uint32_t cache_disabled, uint32_t write_through,
                           uint32_t super_user, uint32_t read_write, uint32_t present)
{
    pde_ptr->words[0] = 0;

    pde_ptr->words[0] |= (page_base_address & 0xffc00000) >> 0;
    pde_ptr->words[0] |= (pat & 0x1) << 12;
    pde_ptr->words[0] |= (avl & 0x7) << 9;
    pde_ptr->words[0] |= (global & 0x1) << 8;
    pde_ptr->words[0] |= (pde_pde_large & 0x1) << 7;
    pde_ptr->words[0] |= (dirty & 0x1) << 6;
    pde_ptr->words[0] |= (accessed & 0x1) << 5;
    pde_ptr->words[0] |= (cache_disabled & 0x1) << 4;
    pde_ptr->words[0] |= (write_through & 0x1) << 3;
    pde_ptr->words[0] |= (super_user & 0x1) << 2;
    pde_ptr->words[0] |= (read_write & 0x1) << 1;
    pde_ptr->words[0] |= (present & 0x1) << 0;
}

PHYS_CODE VISIBLE void
init_boot_pd(void)
{
    word_t i;

    /* identity mapping from 0 up to PPTR_BASE (virtual address) */
    for (i = 0; i < (PPTR_BASE >> seL4_LargePageBits); i++) {
        pde_pde_large_ptr_new_phys(
            _boot_pd + i,
            i << seL4_LargePageBits, /* physical address */
            0, /* pat            */
            0, /* avl            */
            1, /* global         */
            0, /* dirty          */
            0, /* accessed       */
            0, /* cache_disabled */
            0, /* write_through  */
            0, /* super_user     */
            1, /* read_write     */
            1  /* present        */
        );
    }

    /* mapping of PPTR_BASE (virtual address) to PADDR_BASE up to end of virtual address space */
    for (i = 0; i < ((-PPTR_BASE) >> seL4_LargePageBits); i++) {
        pde_pde_large_ptr_new_phys(
            _boot_pd + i + (PPTR_BASE >> seL4_LargePageBits),
            (i << seL4_LargePageBits) + PADDR_BASE, /* physical address */
            0, /* pat            */
            0, /* avl            */
            1, /* global         */
            0, /* dirty          */
            0, /* accessed       */
            0, /* cache_disabled */
            0, /* write_through  */
            0, /* super_user     */
            1, /* read_write     */
            1  /* present        */
        );
    }
}

BOOT_CODE void
map_it_pt_cap(cap_t vspace_cap, cap_t pt_cap)
{
    pde_t* pd   = PDE_PTR(pptr_of_cap(vspace_cap));
    pte_t* pt   = PTE_PTR(cap_page_table_cap_get_capPTBasePtr(pt_cap));
    vptr_t vptr = cap_page_table_cap_get_capPTMappedAddress(pt_cap);

    assert(cap_page_table_cap_get_capPTIsMapped(pt_cap));
    pde_pde_small_ptr_new(
        pd + (vptr >> seL4_LargePageBits),
        pptr_to_paddr(pt), /* pt_base_address */
        0,                 /* avl             */
        0,                 /* accessed        */
        0,                 /* cache_disabled  */
        0,                 /* write_through   */
        1,                 /* super_user      */
        1,                 /* read_write      */
        1                  /* present         */
    );
    invalidatePageStructureCache();
}

BOOT_CODE void
map_it_pd_cap(cap_t vspace_cap, cap_t pd_cap)
{
    /* this shouldn't be called, and it does nothing */
    fail("Should not be called");
}

BOOT_CODE void
map_it_frame_cap(cap_t pd_cap, cap_t frame_cap)
{
    pte_t* pt;
    pde_t* pd    = PDE_PTR(pptr_of_cap(pd_cap));
    void*  frame = (void*)cap_frame_cap_get_capFBasePtr(frame_cap);
    vptr_t vptr  = cap_frame_cap_get_capFMappedAddress(frame_cap);

    assert(cap_frame_cap_get_capFMappedASID(frame_cap) != 0);
    pd += (vptr >> seL4_LargePageBits);
    pt = paddr_to_pptr(pde_pde_small_ptr_get_pt_base_address(pd));
    pte_ptr_new(
        pt + ((vptr & MASK(seL4_LargePageBits)) >> seL4_PageBits),
        pptr_to_paddr(frame), /* page_base_address */
        0,                    /* avl               */
        0,                    /* global            */
        0,                    /* pat               */
        0,                    /* dirty             */
        0,                    /* accessed          */
        0,                    /* cache_disabled    */
        0,                    /* write_through     */
        1,                    /* super_user        */
        1,                    /* read_write        */
        1                     /* present           */
    );
    invalidatePageStructureCache();
}

/* ==================== BOOT CODE FINISHES HERE ==================== */

lookupPDSlot_ret_t lookupPDSlot(vspace_root_t *vspace, vptr_t vptr)
{
    lookupPDSlot_ret_t pdSlot;
    pde_t *pd = PDE_PTR(vspace);
    unsigned int pdIndex;

    pdIndex = vptr >> (PAGE_BITS + PT_INDEX_BITS);
    pdSlot.status = EXCEPTION_NONE;
    pdSlot.pdSlot = pd + pdIndex;
    return pdSlot;
}

bool_t CONST isVTableRoot(cap_t cap)
{
    return cap_get_capType(cap) == cap_page_directory_cap;
}

bool_t CONST isValidNativeRoot(cap_t cap)
{
    return isVTableRoot(cap) &&
           cap_page_directory_cap_get_capPDIsMapped(cap);
}

vspace_root_t *getValidNativeRoot(cap_t vspace_cap)
{
    if (isValidNativeRoot(vspace_cap)) {
        return PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(vspace_cap));
    }
    return NULL;
}

void copyGlobalMappings(vspace_root_t* new_vspace)
{
    word_t i;
    pde_t *newPD = (pde_t*)new_vspace;

    for (i = PPTR_BASE >> seL4_LargePageBits; i < BIT(PD_INDEX_BITS); i++) {
        newPD[i] = ia32KSGlobalPD[i];
    }
}

exception_t performASIDPoolInvocation(asid_t asid, asid_pool_t* poolPtr, cte_t* vspaceCapSlot)
{
    cap_page_directory_cap_ptr_set_capPDMappedASID(&vspaceCapSlot->cap, asid);
    cap_page_directory_cap_ptr_set_capPDIsMapped(&vspaceCapSlot->cap, 1);
    poolPtr->array[asid & MASK(asidLowBits)] = PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(vspaceCapSlot->cap));

    return EXCEPTION_NONE;
}

void unmapPageDirectory(asid_t asid, vptr_t vaddr, pde_t *pd)
{
    deleteASID(asid, pd);
}

static exception_t
performIA32PageDirectoryGetStatusBits(lookupPTSlot_ret_t ptSlot, lookupPDSlot_ret_t pdSlot)
{
    if (pdSlot.status == EXCEPTION_NONE &&
            ((pde_ptr_get_page_size(pdSlot.pdSlot) == pde_pde_large) &&
             pde_pde_large_ptr_get_present(pdSlot.pdSlot))) {

        setRegister(NODE_STATE(ksCurThread), msgRegisters[0], pde_pde_large_ptr_get_accessed(pdSlot.pdSlot));
        setRegister(NODE_STATE(ksCurThread), msgRegisters[1], pde_pde_large_ptr_get_dirty(pdSlot.pdSlot));
        return EXCEPTION_NONE;
    }

    assert(ptSlot.status == EXCEPTION_NONE && pte_ptr_get_present(ptSlot.ptSlot));

    setRegister(NODE_STATE(ksCurThread), msgRegisters[0], pte_ptr_get_accessed(ptSlot.ptSlot));
    setRegister(NODE_STATE(ksCurThread), msgRegisters[1], pte_ptr_get_dirty(ptSlot.ptSlot));

    return EXCEPTION_NONE;
}

exception_t
decodeIA32PageDirectoryInvocation(
    word_t invLabel,
    word_t length,
    cte_t* cte,
    cap_t cap,
    extra_caps_t excaps,
    word_t* buffer
)
{

    switch (invLabel) {
    case X86PageDirectoryGetStatusBits: {
        word_t vaddr;
        vspace_root_t *vspace;
        lookupPTSlot_ret_t ptSlot;
        lookupPDSlot_ret_t pdSlot;

        if (length < 1) {
            userError("X86PageDirectoryGetStatusBits: Truncated message");
            current_syscall_error.type = seL4_TruncatedMessage;

            return EXCEPTION_SYSCALL_ERROR;
        }

        vaddr = getSyscallArg(0, buffer);

        if (vaddr >= PPTR_USER_TOP) {
            userError("X86PageDirectoryGetStatusBits: address inside kernel window");
            current_syscall_error.type = seL4_InvalidArgument;
            current_syscall_error.invalidArgumentNumber = 0;

            return EXCEPTION_SYSCALL_ERROR;
        }

        vspace = (vspace_root_t*)pptr_of_cap(cap);

        /* perform both lookups */
        pdSlot = lookupPDSlot(vspace, vaddr);
        ptSlot = lookupPTSlot(vspace, vaddr);

        /* need either a valid PD mapping or PT mapping */
        if ((pdSlot.status != EXCEPTION_NONE ||
                ((pde_ptr_get_page_size(pdSlot.pdSlot) != pde_pde_large) ||
                 !pde_pde_large_ptr_get_present(pdSlot.pdSlot))) &&
                (ptSlot.status != EXCEPTION_NONE ||
                 (!pte_ptr_get_present(ptSlot.ptSlot)))) {
            userError("X86PageDirectoryGetStatusBits: No mapping found");

            current_syscall_error.type = seL4_InvalidArgument;
            current_syscall_error.invalidArgumentNumber = 1;

            return EXCEPTION_SYSCALL_ERROR;
        }

        setThreadState(NODE_STATE(ksCurThread), ThreadState_Restart);
        return performIA32PageDirectoryGetStatusBits(ptSlot, pdSlot);
    }

    default:
        userError("decodeIA32PageDirectoryInvocation: illegal operation");
        current_syscall_error.type = seL4_IllegalOperation;

        return EXCEPTION_SYSCALL_ERROR;
    }
}

#ifdef CONFIG_PRINTING
typedef struct readWordFromVSpace_ret {
    exception_t status;
    word_t value;
} readWordFromVSpace_ret_t;

static readWordFromVSpace_ret_t
readWordFromVSpace(vspace_root_t *vspace, word_t vaddr)
{
    readWordFromVSpace_ret_t ret;
    lookupPTSlot_ret_t ptSlot;
    lookupPDSlot_ret_t pdSlot;
    paddr_t paddr;
    word_t offset;
    pptr_t kernel_vaddr;
    word_t *value;

    pdSlot = lookupPDSlot(vspace, vaddr);
    if (pdSlot.status == EXCEPTION_NONE &&
            ((pde_ptr_get_page_size(pdSlot.pdSlot) == pde_pde_large) &&
             pde_pde_large_ptr_get_present(pdSlot.pdSlot))) {

        paddr = pde_pde_large_ptr_get_page_base_address(pdSlot.pdSlot);
        offset = vaddr & MASK(seL4_LargePageBits);
    } else {
        ptSlot = lookupPTSlot(vspace, vaddr);
        if (ptSlot.status == EXCEPTION_NONE && pte_ptr_get_present(ptSlot.ptSlot)) {
            paddr = pte_ptr_get_page_base_address(ptSlot.ptSlot);
            offset = vaddr & MASK(seL4_PageBits);
        } else {
            ret.status = EXCEPTION_LOOKUP_FAULT;
            return ret;
        }
    }


    kernel_vaddr = (word_t)paddr_to_pptr(paddr);
    value = (word_t*)(kernel_vaddr + offset);
    ret.status = EXCEPTION_NONE;
    ret.value = *value;
    return ret;
}

void
Arch_userStackTrace(tcb_t *tptr)
{
    cap_t threadRoot;
    vspace_root_t *vspace_root;
    word_t sp;
    int i;

    threadRoot = TCB_PTR_CTE_PTR(tptr, tcbVTable)->cap;

    /* lookup the PD */
    if (cap_get_capType(threadRoot) != cap_page_directory_cap) {
        printf("Invalid vspace\n");
        return;
    }

    vspace_root = (vspace_root_t*)pptr_of_cap(threadRoot);

    sp = getRegister(tptr, ESP);
    /* check for alignment so we don't have to worry about accessing
     * words that might be on two different pages */
    if (!IS_ALIGNED(sp, WORD_SIZE_BITS)) {
        printf("ESP not aligned\n");
        return;
    }

    for (i = 0; i < CONFIG_USER_STACK_TRACE_LENGTH; i++) {
        word_t address = sp + (i * sizeof(word_t));
        readWordFromVSpace_ret_t result;
        result = readWordFromVSpace(vspace_root, address);
        if (result.status == EXCEPTION_NONE) {
            printf("0x%lx: 0x%lx\n", (long)address, (long)result.value);
        } else {
            printf("0x%lx: INVALID\n", (long)address);
        }
    }
}
#endif

#endif
