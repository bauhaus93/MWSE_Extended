#pragma once

#include "FUNCTION.h"
#include "TES3MACHINE.h"

#include "Main.h"

class GetDungeonFunction : public FUNCTION{
	TES3MACHINE& machine;

public:
					GetDungeonFunction(TES3MACHINE& vm) : machine(vm){}
	virtual bool	execute();
};

