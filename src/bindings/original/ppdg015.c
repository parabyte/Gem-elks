

#include "ppdgem.h"
#include "ppdg0.h"



	WORD
evnt_multi(flags, bclk, bmsk, bst, m1flags, m1x, m1y, m1w, m1h, 
		m2flags, m2x, m2y, m2w, m2h, mepbuff,
		tlc, thc, pmx, pmy, pmb, pks, pkr, pbr )
	UWORD		flags, bclk, bmsk, bst;
	UWORD		m1flags, m1x, m1y, m1w, m1h;
	UWORD		m2flags, m2x, m2y, m2w, m2h;
	LPVOID		mepbuff;
	UWORD		tlc, thc;
	UWORD		*pmx, *pmy, *pmb, *pks, *pkr, *pbr;
{
	MU_FLAGS = flags;

	MB_CLICKS = bclk;
	MB_MASK = bmsk;
	MB_STATE = bst;

	MMO1_FLAGS = m1flags;
	MMO1_X = m1x;
	MMO1_Y = m1y;
	MMO1_WIDTH = m1w;
	MMO1_HEIGHT = m1h;

	MMO2_FLAGS = m2flags;
	MMO2_X = m2x;
	MMO2_Y = m2y;
	MMO2_WIDTH = m2w;
	MMO2_HEIGHT = m2h;

	gem_bindings_store_pointer(&MME_PBUFF, mepbuff);

	MT_LOCOUNT = tlc;
	MT_HICOUNT = thc;

	gem_if(EVNT_MULTI);

	*pmx = EV_MX;
	*pmy = EV_MY;
	*pmb = EV_MB;
	*pks = EV_KS;
	*pkr = EV_KRET;
	*pbr = EV_BRET;
	return((WORD) RET_CODE );
}

/*
 * Gemos native applications can keep DS separate from SS.  The historical
 * entry point above cannot represent that because its message pointer is
 * widened through SS while its result pointers are dereferenced through DS.
 * This additive entry point makes the single caller DS authoritative for all
 * seven retained objects, avoiding both implicit widening and far-C spills.
 */
WORD
evnt_multi_ds(UWORD flags, UWORD bclk, UWORD bmsk, UWORD bst,
	UWORD m1flags, UWORD m1x, UWORD m1y, UWORD m1w, UWORD m1h,
	UWORD m2flags, UWORD m2x, UWORD m2y, UWORD m2w, UWORD m2h,
	LPVOID mepbuff, UWORD tlc, UWORD thc, UWORD *pmx, UWORD *pmy,
	UWORD *pmb, UWORD *pks, UWORD *pkr, UWORD *pbr)
{
	MU_FLAGS = flags;
	MB_CLICKS = bclk;
	MB_MASK = bmsk;
	MB_STATE = bst;
	MMO1_FLAGS = m1flags;
	MMO1_X = m1x;
	MMO1_Y = m1y;
	MMO1_WIDTH = m1w;
	MMO1_HEIGHT = m1h;
	MMO2_FLAGS = m2flags;
	MMO2_X = m2x;
	MMO2_Y = m2y;
	MMO2_WIDTH = m2w;
	MMO2_HEIGHT = m2h;
	/*
	 * All seven pointers are retained objects in the caller's DS.  Store that
	 * segment explicitly instead of allowing a near parameter to be widened
	 * with SS by the historical binding.
	 */
	MME_PBUFF.lo = (UWORD) mepbuff;
	MME_PBUFF.hi = mepbuff ? gem_bindings_data_segment() : 0U;
	MT_LOCOUNT = tlc;
	MT_HICOUNT = thc;
	gem_if(EVNT_MULTI);
	*pmx = EV_MX;
	*pmy = EV_MY;
	*pmb = EV_MB;
	*pks = EV_KS;
	*pkr = EV_KRET;
	*pbr = EV_BRET;
	return((WORD) RET_CODE);
}
