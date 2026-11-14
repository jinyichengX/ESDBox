#include "ipgui_memory.h"

/* the global heap */
u8_t ipgui_memheap[IPGUI_SMEM_SIZE] IPGUI_ST_ALIGN(IPGUI_MEM_ALIGN_SIZE);
ipgui_mem_t * ipgui_smem = (ipgui_mem_t *)0;

/* align the addr or size up and down */
#define IPGUI_MEM_ALIGNED_UP(n)  				            (((n) + IPGUI_MEM_ALIGN_SIZE - 1) & (~(ipgui_mem_unit_type_t)IPGUI_MEM_ALIGN_SIZE_MASK))
#define IPGUI_MEM_ALIGNED_DOWN(n)   			            ((n) & (~(ipgui_mem_unit_type_t)IPGUI_MEM_ALIGN_SIZE_MASK))

/* some macros */
#define IPGUI_MEM_MNGHEADSZ_MIN 				            (IPGUI_MEM_ALIGNED_UP(sizeof(ipgui_mem_t)))
#define IPGUI_MEM_MNGNODEESZ_MIN 				            (IPGUI_MEM_ALIGNED_UP(sizeof(linknode_t)))
#define IPGUI_MEM_MNGSZ_MIN 					            (IPGUI_MEM_MNGHEADSZ_MIN + IPGUI_MEM_MNGNODEESZ_MIN)
#define IPGUI_MEM_ALLOC_FIX_HEAD_MIN 			            (IPGUI_MEM_ALIGNED_UP(sizeof(struct usdinfo)))
#define IPGUI_MEM_ALLOCSZ_MIN                               (IPGUI_MEM_MNGNODEESZ_MIN)

/* set allocate pattern */
#define IPGUI_MEM_ALLOCPATTERN_SET(pstMem, md) 		        ((pstMem)->strat.alloc_pttn = (AllocStrategy_t)md)

/* insert node operations */
#define IPGUI_MEM_ADDTO_FREELIST(pstHead, pstNode) 		    (list_add((pstNode), (pstHead)))
#define IPGUI_MEM_ADDTO_FREELIST_TAIL(pstHead, pstNode) 	(list_add_tail((pstNode), (pstHead)))
#define IPGUI_MEM_ADDTO_FREELIST_BTWN(pnew, pprev, pnext)	(__list_add((pnew), (pprev), (pnext)))

/* delate node operation */
#define IPGUI_MEM_DEL_FREELIST(pstNode)				        (list_del((pstNode)))

/* if free list empty */
#define IPGUI_MEM_FREELIST_EMPTY(pstHead) 			        (list_empty((pstHead)))

/* repalce node */
#define IPGUI_MEM_REPLACE_NODE(pold, pnew) 			        (list_replace((pold), (pnew)))

/* if free list has only one node */
#define IPGUI_MEM_FREENODE_SINGLE(pstHead) 			        ((!list_empty((pstHead)))&&((pstHead)->next==(pstHead)->prev))

/* traversal free list */
#define IPGUI_MEM_TRAVERSAL_FREELIST				        list_for_each_safe

#define IPGUI_MEM_DBG_WARNING

static void * ipgui_mem_alloc_bf(ipgui_mem_t *, ipgui_mem_unit_type_t);
static void * ipgui_mem_alloc_ff(ipgui_mem_t *, ipgui_mem_unit_type_t);
static void * ipgui_mem_alloc_wf(ipgui_mem_t *, ipgui_mem_unit_type_t);

/* initialise kernel heap memory */
__IPGUI_API__ ipgui_mem_t * ipgui_mem_init(void * pvSurf, void * pvBottom)
{
	ipgui_mem_t * pstMem = (ipgui_mem_t *)0;
	linknode_t * pstFirstNode;
	ipgui_mem_unit_type_t AlgSurf, AlgBtm;
	
	if ((pvSurf == (void *)0) || (pvBottom == (void *)0))
	    return (ipgui_mem_t *)0;
	if (IPGUI_MEM_MNGNODEESZ_MIN < IPGUI_MEM_ALLOC_FIX_HEAD_MIN)
	    return (ipgui_mem_t *)0;
	
	AlgSurf = (ipgui_mem_unit_type_t)IPGUI_MEM_ALIGNED_UP(( ipgui_mem_unit_type_t )pvSurf);
	AlgBtm  = (ipgui_mem_unit_type_t)IPGUI_MEM_ALIGNED_DOWN(( ipgui_mem_unit_type_t )pvBottom);
	
	if ((AlgBtm - AlgSurf) < IPGUI_MEM_MNGSZ_MIN)
	    return (ipgui_mem_t *)0;
	
	pstMem = (ipgui_mem_t *)AlgSurf;
	pstMem->StartAddr = AlgSurf;
	pstMem->HeapSize  = AlgBtm - AlgSurf;
	pstMem->ValidSize = pstMem->HeapSize - IPGUI_MEM_MNGHEADSZ_MIN;
	pstMem->strat.php_alloc[BEST_FIT]  = ipgui_mem_alloc_bf;
	pstMem->strat.php_alloc[FIRST_FIT] = ipgui_mem_alloc_ff;
	pstMem->strat.php_alloc[WORST_FIT] = ipgui_mem_alloc_wf;
#if IPGUI_MEM_STATISTICS_EN == 1
	pstMem->bUsageInt  = 0;
	pstMem->bUsageFlo  = 0;
#endif

	IPGUI_MEM_ALLOCPATTERN_SET(pstMem, FIRST_FIT);

	pstFirstNode = (linknode_t *)(AlgSurf + IPGUI_MEM_MNGHEADSZ_MIN);
	pstFirstNode->BlockSize = pstMem->ValidSize;
	
	list_head_init(&pstFirstNode->stBlockLink);
	list_head_init(&pstMem->stFreeEntry);
	IPGUI_MEM_ADDTO_FREELIST(&pstFirstNode->stBlockLink, &pstMem->stFreeEntry);
	
	return pstMem;
}

/* best fit */
static void * ipgui_mem_alloc_bf(ipgui_mem_t * pstMem, ipgui_mem_unit_type_t Size)
{
	linknode_t * pstFitNode = (linknode_t *)0;
	struct list_head * pstPos, * pstNext;
	ipgui_mem_unit_type_t TempSize = 0xffffffff;
	
	IPGUI_MEM_TRAVERSAL_FREELIST(pstPos, pstNext, &pstMem->stFreeEntry)
    {
        if ((((linknode_t *)pstPos)->BlockSize >= Size) && (TempSize >= ((linknode_t *)pstPos)->BlockSize))
        {
            pstFitNode = (linknode_t *)pstPos;
            TempSize = ((linknode_t *)pstPos)->BlockSize;
            if( TempSize == Size )
                break;
        }
	}
	return (void *)pstFitNode;
}

/* first fit */
static void * ipgui_mem_alloc_ff( ipgui_mem_t * pstMem, ipgui_mem_unit_type_t Size )
{
	linknode_t * pstFitNode = (linknode_t *)0;
	struct list_head * pstPos, * pstNext;
	
	IPGUI_MEM_TRAVERSAL_FREELIST(pstPos, pstNext, &pstMem->stFreeEntry)
    {
        if (((linknode_t *)pstPos)->BlockSize >= Size)
        {
            pstFitNode = (linknode_t *)pstPos;
            break;
        }
	}
	return (void *)pstFitNode;
}

/* worst fit */
static void * ipgui_mem_alloc_wf( ipgui_mem_t * pstMem, ipgui_mem_unit_type_t Size )
{
	linknode_t * pstFitNode = (linknode_t *)0;
	struct list_head * pstPos, * pstNext;
	ipgui_mem_unit_type_t TempSize = 0;
	
	IPGUI_MEM_TRAVERSAL_FREELIST( pstPos, pstNext, &pstMem->stFreeEntry )
    {
        if ((((linknode_t *)pstPos)->BlockSize >= Size) && (TempSize <= ((linknode_t *)pstPos)->BlockSize))
        {
            pstFitNode = (linknode_t *)pstPos;
            TempSize = ((linknode_t *)pstPos)->BlockSize;
            if( TempSize == Size )
                break;
        }
	}
	return (void *)pstFitNode;
}

/* allocate block from heap */
__IPGUI_API__ void * ipgui_mem_alloc(ipgui_mem_t * pstMem, ipgui_mem_unit_type_t Size)
{
	char ptn;
	ipgui_mem_unit_type_t NeedSz;
	int leftsz;
	linknode_t * pstFitNode, * pstNextNode;
	struct list_head stBk;
	struct usdinfo * alloc;
	
	if ((pstMem == (ipgui_mem_t *)0) || (!Size))
        return (void *)0;
	if (IPGUI_MEM_FREELIST_EMPTY(&pstMem->stFreeEntry))
        return (void *)0;
	
	NeedSz = IPGUI_MEM_ALIGNED_UP(Size);
	NeedSz += IPGUI_MEM_ALLOC_FIX_HEAD_MIN;
	NeedSz = (NeedSz < IPGUI_MEM_ALLOCSZ_MIN) ? (IPGUI_MEM_ALLOCSZ_MIN) : (NeedSz);

	ptn = (char)pstMem->strat.alloc_pttn;
	pstFitNode = ( linknode_t * )(pstMem->strat.php_alloc[ptn](pstMem, NeedSz));
    
#if IPGUI_MEM_ALLOC_PATTERN_AUTO_SWITCH == 1
	/* 
	 * if worst fit allocate failed return 
	 * but if not,switch to the other allocate pattern 
	 * and try again
	 */
	if ((pstFitNode == (linknode_t *)0)&&(ptn == WORST_FIT))
	    return (void *)0;

	else if ((pstFitNode == (linknode_t *)0) && (ptn != WORST_FIT))
    {
        for( idx = 0; idx < MAX_PATTERN; idx++ )
        {
            if( idx == ptn )
                continue;

            IPGUI_MEM_ALLOCPATTERN_SET(pstMem, idx);

            if ((pstFitNode = (linknode_t *)pstMem->strat.php_alloc[ptn](pstMem, NeedSz)) != (linknode_t *)0)
                break;
        }
        IPGUI_MEM_ALLOCPATTERN_SET(pstMem, ptn);
	}
#endif

	if (pstFitNode == (linknode_t *)0)
        return (void *)0;
	
	leftsz = pstFitNode->BlockSize - NeedSz;
	if (leftsz >= IPGUI_MEM_MNGNODEESZ_MIN)
    {
        pstNextNode = (linknode_t *)((char *)pstFitNode + NeedSz);
        stBk.next = pstFitNode->stBlockLink.next;
        stBk.prev = pstFitNode->stBlockLink.prev;

        IPGUI_MEM_REPLACE_NODE(&stBk, &pstNextNode->stBlockLink);
        pstNextNode->BlockSize = leftsz;
	}
	else
    {
        IPGUI_MEM_DEL_FREELIST((struct list_head *)pstFitNode);
        NeedSz = pstFitNode->BlockSize;
	}
	
	alloc = (struct usdinfo *)pstFitNode;
	alloc->UsedSize = NeedSz;

	return (void *)(((char *)pstFitNode) + IPGUI_MEM_ALLOC_FIX_HEAD_MIN);
}


/* allocate n aligned block from heap */ 
__IPGUI_API__ void * ipgui_mem_alloc_align(ipgui_mem_t * pstMem, ipgui_mem_unit_type_t ** unaligned_addr, ipgui_mem_unit_type_t align_size, int n)
{
	ipgui_mem_unit_type_t Size;
	void * p = (void *)0;
	
	if ((pstMem == (ipgui_mem_t *)0) || (!align_size) || (n == 0))
        return (void *)0;
	if (align_size % IPGUI_MEM_ALIGN_SIZE)
        return (void *)0;
	
	if (unaligned_addr == (ipgui_mem_unit_type_t **)0) 
        IPGUI_MEM_DBG_WARNING;
	
	Size = align_size * (n + 1);
	if ((void *)0 == (p = ipgui_mem_alloc(pstMem, Size)))
        return (void *)0;
	
	* unaligned_addr = p;
	p = (void *)((((ipgui_mem_unit_type_t)p) + align_size-1) & (~(ipgui_mem_unit_type_t)(align_size-1)));
	
	return (void *)p;
}

/* allocate and clear block */
__IPGUI_API__ void * ipgui_mem_calloc(ipgui_mem_t * pstMem, ipgui_mem_unit_type_t Size)
{
	void * pvAddr = (void *)0;
	
	if ((pstMem == (ipgui_mem_t *)0) || (!Size))
        return (void *)0;

	if ((void *)0 != (pvAddr = ipgui_mem_alloc(pstMem, Size)))
        ipgui_memset(pvAddr, 0, Size);
	
	return (void *)pvAddr;
}

/* record neighbour free node of block to free */
static void search_neigh_node(ipgui_mem_t * pstMem, void * pvFstAddr, struct list_head ** ppstNeigh)
{
	struct list_head *pstPos,*pstNext;
	
	if ((pstMem == (ipgui_mem_t *)0) || (pvFstAddr == (void *)0))
        return;
	if (IPGUI_MEM_FREELIST_EMPTY(&pstMem->stFreeEntry))
        return;
	
	IPGUI_MEM_TRAVERSAL_FREELIST(pstPos, pstNext, &pstMem->stFreeEntry)
    {
        if ((ipgui_mem_unit_type_t)pstPos < (ipgui_mem_unit_type_t)pvFstAddr)
            * ppstNeigh = (struct list_head *)pstPos;

        if ((ipgui_mem_unit_type_t)pstPos > (ipgui_mem_unit_type_t)pvFstAddr)
        {
            * (ppstNeigh + 1) = (struct list_head *)pstPos;
            break;
        }
	}
}

/* free block from heap */
__IPGUI_API__ void ipgui_mem_free(ipgui_mem_t * pstMem, void * pvFstAddr)
{
	ipgui_mem_unit_type_t Size;
	linknode_t * pstFreeNode;
	struct list_head stBk;
	struct list_head * pstaNeighNode[2] = { [0] = 0,[1] = 0, };
	if ((pstMem == (ipgui_mem_t *)0) || (pvFstAddr == (void *)0))
        return;

	pstFreeNode = (linknode_t *)((char *)pvFstAddr - IPGUI_MEM_ALLOC_FIX_HEAD_MIN);
	Size = *((ipgui_mem_unit_type_t *)pstFreeNode);
	
	search_neigh_node(pstMem, pvFstAddr, pstaNeighNode);

	if (pstaNeighNode[0] && pstaNeighNode[1])
    {
        if ((void *)((char *)pstaNeighNode[0] + ((linknode_t *)pstaNeighNode[0])->BlockSize) == (void *)pstFreeNode)
        {
            ((linknode_t *)pstaNeighNode[0])->BlockSize += Size;

            if ((((linknode_t *)pstaNeighNode[0])->BlockSize + (char *)pstaNeighNode[0]) == (void *)pstaNeighNode[1])
            {
                ((linknode_t *)pstaNeighNode[0])->BlockSize += ((linknode_t *)pstaNeighNode[1])->BlockSize;
                IPGUI_MEM_DEL_FREELIST(pstaNeighNode[1]);
            }
        }
        else
        {
            if ((void *)((char *)pstFreeNode + Size) == (void *)pstaNeighNode[1])
            {
                stBk.next = pstaNeighNode[1]->next;
                stBk.prev = pstaNeighNode[1]->prev;
                IPGUI_MEM_REPLACE_NODE(&stBk, &pstFreeNode->stBlockLink);
                pstFreeNode->BlockSize = Size + ((linknode_t *)pstaNeighNode[1])->BlockSize;
            }
            else
            {
                pstFreeNode->BlockSize = Size;
                IPGUI_MEM_ADDTO_FREELIST_BTWN(&pstFreeNode->stBlockLink, pstaNeighNode[0], pstaNeighNode[1]);
            }
        }
	}
	else if (pstaNeighNode[0])
    {
        if ((void *)((char *)pstaNeighNode[0] + ((linknode_t *)pstaNeighNode[0])->BlockSize) == (void *)pstFreeNode)
            ((linknode_t *)pstaNeighNode[0])->BlockSize += Size;
        else
        {
            IPGUI_MEM_ADDTO_FREELIST_TAIL(&pstMem->stFreeEntry, &pstFreeNode->stBlockLink);
            pstFreeNode->BlockSize = Size;
        }
	}
	else if (pstaNeighNode[1])
    {
        if ((void *)((char *)pstFreeNode + Size) == (void *)pstaNeighNode[1])
        {
            stBk.next = pstaNeighNode[1]->next;
            stBk.prev = pstaNeighNode[1]->prev;
            IPGUI_MEM_REPLACE_NODE(&stBk, &pstFreeNode->stBlockLink);
            pstFreeNode->BlockSize = Size + ((linknode_t *)pstaNeighNode[1])->BlockSize;
        }
        else
        {
            IPGUI_MEM_ADDTO_FREELIST(&pstMem->stFreeEntry, &pstFreeNode->stBlockLink);
            pstFreeNode->BlockSize = Size;
        }
	}
	else if ((!pstaNeighNode[0]) && (!pstaNeighNode[1]))
    {
	    IPGUI_MEM_ADDTO_FREELIST(&pstMem->stFreeEntry, &pstFreeNode->stBlockLink);
	    pstFreeNode->BlockSize = Size;
	}
}

#include "ipgui_debug.h"
static void mem_usage_calc(ipgui_mem_t * pstMem);
/* realloc memory block */
__IPGUI_API__ void * ipgui_mem_realloc(ipgui_mem_t * pstMem, void * pvFstAddr, ipgui_mem_unit_type_t Size)
{
	int nOrgSz = 0;
	void * pvNewAddr = (void *)0;

	if ((pstMem == (ipgui_mem_t *)0))
        return (void *)0;
	if (!Size)
        return (void *)pvFstAddr;
    if (pvFstAddr == (void *)0)
        return ipgui_mem_alloc(pstMem, Size);

	nOrgSz = ((struct usdinfo *)((char *)pvFstAddr - IPGUI_MEM_ALLOC_FIX_HEAD_MIN))->UsedSize;
    nOrgSz -= IPGUI_MEM_ALLOC_FIX_HEAD_MIN;
    if (Size == nOrgSz)
        return (void *)pvFstAddr;
	
    pvNewAddr = ipgui_mem_alloc(pstMem, Size);
    if (pvNewAddr == (void *)0)
        return (void *)pvFstAddr;
	
    ipgui_memcpy(pvNewAddr, pvFstAddr, (nOrgSz < Size) ? nOrgSz : Size);
    ipgui_mem_free(pstMem, pvFstAddr);
#if 1
    mem_usage_calc(pstMem);
    ipgui_dbg_info("ipgui heap usage: %d.%d%%\n", pstMem->bUsageInt, pstMem->bUsageFlo);
#endif

	return (void *)pvNewAddr;
}

#if IPGUI_MEM_STATISTICS_EN == 1

static void calc_usage(ipgui_mem_unit_type_t Used, ipgui_mem_unit_type_t Total, char * seg_int, char * seg_flo)
{
	(* seg_int) = (Used * 100) / Total;
	(* seg_flo) = ((Used * 100) % Total) * 100 / Total;
}

/* calculate usage of heap */
static void mem_usage_calc(ipgui_mem_t *pstMem)
{
	struct list_head *pstPos, *pstNext;
    linknode_t *pstNode;
    ipgui_mem_unit_type_t Total = pstMem->ValidSize;
    ipgui_mem_unit_type_t Used = 0;
	
    if (pstMem == (ipgui_mem_t *)0)
        return;

    IPGUI_MEM_TRAVERSAL_FREELIST(pstPos, pstNext, &pstMem->stFreeEntry)
    {
        pstNode = list_entry(pstPos, linknode_t, stBlockLink);
        Used += pstNode->BlockSize;
    }
	
    Used = Total - Used;
	if ((Used) || (!Used))
	    calc_usage(Used, Total, &pstMem->bUsageInt, &pstMem->bUsageFlo);
}

#endif

/* take heap statistics */
__IPGUI_API__ ipgui_err_t ipgui_mem_statistics_take(ipgui_mem_t * pstMem, ipgui_mem_unit_type_t *HeapSize, ipgui_mem_unit_type_t * StartAddr, u8_t * pubStrat, u8_t * pubUsg1, u8_t * pubUsg2)
{
	if ((pstMem == (ipgui_mem_t *)0))
        return IPGUI_ERR_PARAM;

	if (HeapSize) * HeapSize = ((ipgui_mem_t *)pstMem)->HeapSize;
	if (StartAddr) * StartAddr = ((ipgui_mem_t *)pstMem)->StartAddr;
	if (pubStrat) * pubStrat = (u8_t)(((ipgui_mem_t *)pstMem)->strat.alloc_pttn);
#if IPGUI_MEM_STATISTICS_EN == 1
	mem_usage_calc( pstMem );
	if (pubUsg1) * pubUsg1 = (u8_t)((ipgui_mem_t *)pstMem)->bUsageInt;
	if (pubUsg2) * pubUsg2 = (u8_t)((ipgui_mem_t *)pstMem)->bUsageFlo;
#endif
	return IPGUI_ERR_OK;
}

static const char us = sizeof(ipgui_mem_unit_type_t);

/* memory set */
__IPGUI_API__ void ipgui_memset(void * pv, u8_t v, u32_t len)
{
    u8_t *dst;
    u32_t i;

    if (!len || !pv) return;
    dst = (u8_t *)pv;

    /* align dst to ptr-width boundary */
    while (len && ((uintptr_t)dst & (sizeof(uintptr_t) - 1))) {
        *dst++ = v;
        len--;
    }

    /* ptr-width word fill */
    if (len >= sizeof(uintptr_t)) {
        uintptr_t w = (uintptr_t)v;
        w |= w << 8;
        w |= w << 16;
        /* expand to 64-bit only on platforms where ptr >= 8 bytes */
        if (sizeof(uintptr_t) >= 8) {
            w |= w << 32;
        }

        uintptr_t *wp = (uintptr_t *)dst;
        u32_t n = len / sizeof(uintptr_t);
        for (i = 0; i < n; i++) {
            *wp++ = w;
        }
        dst = (u8_t *)wp;
        len %= sizeof(uintptr_t);
    }

    /* tail bytes */
    for (i = 0; i < len; i++) {
        *dst++ = v;
    }
}

__IPGUI_API__ void ipgui_memset_0(void * pv, u32_t len)
{
    ipgui_memset(pv, 0, len);
}

__IPGUI_API__ int ipgui_strlen(const char * str)
{
    int len = 0;
    while (str[len] != '\0')
        len ++;
    return len;
}

__IPGUI_API__ void ipgui_memcpy(void *dst, const void *src, u32_t len)
{
    u8_t *d;
    const u8_t *s;
    u32_t i;

    if (!len || !dst || !src) return;

    d = (u8_t *)dst;
    s = (const u8_t *)src;

    /* 小长度快速路径 */
    if (len <= sizeof(uintptr_t)) {
        while (len--) {
            *d++ = *s++;
        }
        return;
    }

    /* 对齐目的地址到机器字边界 */
    while (len && ((uintptr_t)d & (sizeof(uintptr_t) - 1))) {
        *d++ = *s++;
        len--;
    }

    /* 仅当源地址也对齐时，才执行字宽拷贝，保证硬件安全 */
    if (len >= sizeof(uintptr_t) && !((uintptr_t)s & (sizeof(uintptr_t) - 1))) {
        uintptr_t *wd = (uintptr_t *)d;
        const uintptr_t *ws = (const uintptr_t *)s;
        const u32_t n = len / sizeof(uintptr_t);

        for (i = 0; i < n; i++) {
            *wd++ = *ws++;
        }

        d = (u8_t *)wd;
        s = (const u8_t *)ws;
        len %= sizeof(uintptr_t);
    }

    /* 尾部剩余字节收尾 */
    while (len--) {
        *d++ = *s++;
    }
}
__IPGUI_API__ __IPGUI_INIT__ ipgui_err_t ipgui_mem_module_init(void)
{
    ipgui_smem = ipgui_mem_init((void*)ipgui_memheap, (void *)(ipgui_memheap + IPGUI_SMEM_SIZE));
    return ipgui_smem ? IPGUI_ERR_OK : IPGUI_ERR_NOK;
}

__IPGUI_API__ void ipgui_mem_usage_statistics_take(u8_t * pubUsg1, u8_t * pubUsg2)
{
    ipgui_mem_statistics_take( ipgui_smem, 0, 0, 0, pubUsg1, pubUsg2);
}

__IPGUI_API__ void * ipgui_mem_alloc_def(ipgui_mem_unit_type_t size)
{
    return ipgui_mem_alloc(ipgui_smem, size);
}

__IPGUI_API__ void ipgui_mem_free_def(void * p)
{
    ipgui_mem_free(ipgui_smem, p);
}