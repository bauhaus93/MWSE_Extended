#include "GenerateDungeonFunction.h"

static DungeonGenerator* gen = NULL;

bool GenerateDungeonFunction::execute(){
	long result = 0;
	long id;
	if (machine.pop(id)){
		result = DungeonDll::Generate(id);
	}
	return machine.push(result);
}