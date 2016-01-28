#pragma once

#include <string>

#include "FUNCTION.h"
#include "TES3MACHINE.h"

#include "Main.h"

class AddTileFactoryFunction: public FUNCTION{
	TES3MACHINE& machine;

public:
					AddTileFactoryFunction(TES3MACHINE& vm) : machine(vm){}
	virtual bool	execute();
};

