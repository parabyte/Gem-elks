

#include "ppdgem.h"
#include "ppdg0.h"




					/* Form Manager			*/
	WORD
form_do(form, start)
	LPTREE		form;
	WORD		start;
{
	gem_bindings_store_pointer(&FM_FORM, form);
	FM_START = start;
	return( gem_if( FORM_DO ) );
}
