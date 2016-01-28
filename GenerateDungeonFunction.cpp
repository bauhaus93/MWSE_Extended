#include "GenerateDungeonFunction.h"

static DungeonGenerator* gen = NULL;

bool GenerateDungeonFunction::execute(){
	return machine.push(666l);
}