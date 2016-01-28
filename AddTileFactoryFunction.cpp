#include "AddTileFactoryFunction.h"

bool AddTileFactoryFunction::execute(){
	long result = 0;
	long dungeon;
	long tileType;
	VMREGTYPE pStr = 0;
	const char *str = NULL;
	float x, y, z;

	if (machine.pop(dungeon) &&
		machine.pop(tileType) &&
		machine.pop(pStr) &&
		(str = machine.GetString((VPVOID)pStr)) != 0 &&
		machine.pop(x) &&
		machine.pop(y) &&
		machine.pop(z)){
		result = (long)DungeonDll::AddTileFactory(dungeon, tileType, str, x, y, z);
	}
	return machine.push(result);
}

