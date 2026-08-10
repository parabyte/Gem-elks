

#include "ppdgem.h"
#include "ppdg0.h"



	WORD
appl_init(LPXBUF lpx )
{
	/* Preserve the original six far-pointer parameter-block slots. */
	gem_bindings_store_pointer(&gb.control, &control[0]);
	gem_bindings_store_pointer(&gb.global, &global[0]);
	gem_bindings_store_pointer(&gb.intin, &int_in[0]);
	gem_bindings_store_pointer(&gb.intout, &int_out[0]);
	gem_bindings_store_pointer(&gb.addrin, &addr_in[0]);
	gem_bindings_store_pointer(&gb.addrout, &addr_out[0]);

	/* [JCE] XBLK parameter, now stored as an explicit offset/segment pair. */
	gem_bindings_store_pointer(&addr_in[0], (LPVOID) lpx);
	
	ad_g =&gb;
	gem_if(APPL_INIT);
	return((WORD) RET_CODE );
}
