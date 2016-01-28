#pragma once

#include "FUNCTION.h"
#include "TES3MACHINE.h"

#include "Main.h"

class AddExpansionFunction : public FUNCTION{
	TES3MACHINE& machine;

public:
					AddExpansionFunction(TES3MACHINE& vm) : machine(vm){}
	virtual bool	execute();
};

