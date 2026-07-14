

#include "ppdgem.h"
#include "ppdg0.h"



	WORD
rsrc_gaddr(rstype, rsid, paddr)
	WORD		rstype;
	WORD		rsid;
	LPVOID		*paddr;
{
	RS_TYPE = rstype;
	RS_INDEX = rsid;
	gem_if(RSRC_GADDR);
#if GEM_TRAP_FAR_DATA
	/*
	 * `paddr` is a near pointer to the caller's four-byte far-pointer
	 * variable.  Copy the original offset and segment words into that
	 * variable directly; assigning through LPVOID would intentionally lose
	 * the segment because ordinary LPVOID remains a hot-path near pointer.
	 */
	if (RET_CODE) {
		((UWORD *) paddr)[0] = RS_OUTADDR.lo;
		((UWORD *) paddr)[1] = RS_OUTADDR.hi;
	} else {
		((UWORD *) paddr)[0] = 0;
		((UWORD *) paddr)[1] = 0;
	}
#else
	*paddr = gem_bindings_load_pointer(&RS_OUTADDR);
#endif
	return((WORD) RET_CODE );
}


	WORD
rsrc_gaddr_far(rstype, rsid, paddr)
	WORD		rstype;
	WORD		rsid;
	GEM_BINDINGS_POINTER_SLOT *paddr;
{
	WORD		result;

	if (!paddr)
		return 0;

	RS_TYPE = rstype;
	RS_INDEX = rsid;
	gem_if(RSRC_GADDR);
	result = (WORD) RET_CODE;
	if (result) {
		/* Copy the exact offset and segment; no pointer arithmetic occurs. */
		paddr->lo = RS_OUTADDR.lo;
		paddr->hi = RS_OUTADDR.hi;
	} else {
		paddr->lo = 0;
		paddr->hi = 0;
	}
	return result;
}
