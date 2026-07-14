

#include "ppdgem.h"
#include "ppdg0.h"



	WORD
shel_write(doex, isgr, iscr, pcmd, ptail)
	WORD		doex, isgr, iscr;
	LPVOID		pcmd, ptail;
{
	SH_DOEX = doex;
	SH_ISGR = isgr;
	SH_ISCR = iscr;
	gem_bindings_store_pointer(&SH_PCMD, pcmd);
	gem_bindings_store_pointer(&SH_PTAIL, ptail);
	return( gem_if( SHEL_WRITE ) );
}
