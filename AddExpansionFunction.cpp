#include "AddExpansionFunction.h"

bool AddExpansionFunction::execute(){
	long result = 0;
	long id;
	float x, y, z;
	long dir;
	if (machine.pop(id) && machine.pop(x) && machine.pop(y) && machine.pop(z) && machine.pop(dir)){
		result = DungeonDll::AddExpansion(id, x, y, z, dir);
	}
	return machine.push(result);
}
