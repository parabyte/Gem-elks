

#include "ppdgem.h"
#include "ppdg0.h"


					/* Resource Manager		*/
	WORD
rsrc_load(rsname)
	LPBYTE	rsname;
{
	gem_bindings_store_pointer(&RS_PFNAME, rsname);
	return( gem_if(RSRC_LOAD) );
}
