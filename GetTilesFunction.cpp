#include "GetTilesFunction.h"


bool GetTilesFunction::execute(){
	long result = 0;
	long id;
	std::string str = "null";
	float posX = 0, posY = 0, posZ = 0;
	float rotX = 0, rotY = 0, rotZ = 0;
	if (machine.pop(id)){
		if (DungeonDll::GetTiles(id, str, posX, posY, posZ, rotX, rotY, rotZ) == 0){
			return machine.push(0l);
		}
	}
	machine.push(rotZ);
	machine.push(rotY);
	machine.push(rotX);
	machine.push(posZ);
	machine.push(posY);
	machine.push(posX);
	machine.push((VMREGTYPE)strings.add(str.c_str()));
	return true;
}


