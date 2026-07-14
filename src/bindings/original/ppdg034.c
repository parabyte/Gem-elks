

#include "ppdgem.h"
#include "ppdg0.h"



	WORD
form_alert(defbut, astring)
	WORD		defbut;
	LPBYTE		astring;
{
	FM_DEFBUT = defbut;
	gem_bindings_store_pointer(&FM_ASTRING, astring);
	return( gem_if( FORM_ALERT ) );
}
