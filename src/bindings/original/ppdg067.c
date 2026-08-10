

#include "ppdgem.h"
#include "ppdg0.h"



	WORD
shel_find(ppath)
	LPVOID		ppath;
{
	gem_bindings_store_pointer(&SH_PATH, ppath);
	return( gem_if( SHEL_FIND ) );
}
