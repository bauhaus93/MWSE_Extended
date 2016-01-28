#pragma once

#include "FUNCTION.h"
#include "TES3MACHINE.h"
#include "STRINGS.h"

#include "Main.h"

class GetTilesFunction: public FUNCTION{
	TES3MACHINE& machine;

public:
					GetTilesFunction(TES3MACHINE& vm) : machine(vm){}
	virtual bool	execute();
};

