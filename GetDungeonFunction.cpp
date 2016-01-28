#include "GetDungeonFunction.h"

bool GetDungeonFunction::execute(){
	return machine.push(DungeonDll::GetDungeon());
}
