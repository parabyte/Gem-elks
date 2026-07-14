

#include "ppdgem.h"
#include "ppdg0.h"



					/* Menu Manager			*/
	WORD
menu_bar(tree, showit)
	LPTREE		tree;
	WORD		showit;
{
	gem_bindings_store_pointer(&MM_ITREE, tree);
	SHOW_IT = showit;
	return( gem_if(MENU_BAR) );
}
