
#include "ppdgem.h"
#include "ppdg0.h"



	WORD
graf_slidebox(tree, parent, obj, isvert)
	LPTREE		tree;
	WORD		parent;
	WORD		obj;
	WORD		isvert;
{
	gem_bindings_store_pointer(&GR_TREE, tree);
	GR_PARENT = parent;
	GR_OBJ = obj;
	GR_ISVERT = isvert;
	return( gem_if( GRAF_SLIDEBOX ) );
}
