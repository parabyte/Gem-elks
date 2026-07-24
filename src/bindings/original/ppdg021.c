

#include "ppdgem.h"
#include "ppdg0.h"



					/* Menu Manager			*/
	WORD
menu_register(pid, pstr)
	WORD		pid;
	LPVOID		pstr;
{
	MM_PID = pid;
	gem_bindings_store_pointer(&MM_PSTR, pstr);
	return( gem_if(MENU_REGISTER) );
}
