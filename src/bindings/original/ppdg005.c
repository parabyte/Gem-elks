
#include "ppdgem.h"
#include "ppdg0.h"



	WORD
appl_find(pname)
	LPVOID		pname;
{
	gem_bindings_store_pointer(&AP_PNAME, pname);
	return( gem_if(APPL_FIND) );
}
