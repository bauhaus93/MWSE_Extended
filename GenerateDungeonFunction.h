#pragma once

#include <iostream>

#include "FUNCTION.h"
#include "TES3MACHINE.h"

#include "Main.h"


class GenerateDungeonFunction :	public FUNCTION{
	TES3MACHINE& machine;

public:
					GenerateDungeonFunction(TES3MACHINE& vm) : machine(vm){}
	virtual bool	execute();
	
};

