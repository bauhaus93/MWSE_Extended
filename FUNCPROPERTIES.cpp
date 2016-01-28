//FuncProperties.cpp
#include <windows.h>
#include <algorithm>
using namespace std;

#include "FUNCPROPERTIES.h"
#include "REFERENCE.h"
#include "TES3MEMMAP.h"
#include "TES3OPCODES.h"
#include "DEBUGGING.h"
#include "cLog.h"
#include "TES3OFFSETS.h"
#include <string.h>
// 22-08-2006 Tp21
#include "warnings.h"

static SPELRecord * GetSpellRecord(VMLONG const spellId, TES3MACHINE & machine);
static ENCHRecord * GetEnchantmentRecord(VMLONG const enchId, TES3MACHINE & machine);
static VMSHORT CountEffects(Effect const * effects);
static Effect * GetEffects(long const type, long const id, TES3MACHINE & machine);
static VMLONG SetEffect(Effect * effects, VMLONG index, VMLONG effect_id, 
						VMLONG skill_attribute_id, VMLONG range, VMLONG area, 
						VMLONG duration, VMLONG minimum_magnitude,
						VMLONG maximum_magnitude);
static float GetSkillRequirement(TES3MACHINE& machine, long skill_id);
static void CheckForLevelUp(long const progress);

// These functions are used for validation in FUNCSETMAX... and for
// retrieving the value in FUNCGETMAX... execute() methods.
static ULONG GetMaxCondition(TES3MACHINE &machine, VPVOID refr, ULONG type);
static ULONG GetMaxCharge(TES3MACHINE &machine, VPVOID refr, ULONG type);

static VPVOID GetMaxChargeOffset(TES3MACHINE &machine, VPVOID refr, ULONG type);

//Tp21 22-08-2006: xGetSpellEffects
//TODO i don't think it works... (we don't get the return results... have to find that breakpoint)
FUNCGETSPELLEFFECTS::FUNCGETSPELLEFFECTS(TES3MACHINE& vm) : machine(vm), HWBREAKPOINT()
{
}

bool FUNCDELETESPELL::execute(void)
{
	// TODO Calling AddSpell on a deleted spell does not cause an error. There
	// must be some other data structure that should be updated when deleting a
	// spell.
	VMLONG spell_id;
	VMLONG result = 0;
	if (machine.pop(spell_id)) {
		char const* id =
			machine.GetString(reinterpret_cast<VPVOID>(spell_id));
		SPELRecord* spell = GetSpellRecord(spell_id, machine);
		if (spell) {
			TES3CELLMASTER* cell_master =
				*(reinterpret_cast<TES3CELLMASTER**>reltolinear(MASTERCELL_IMAGE));
			LinkedList* spells_list = cell_master->recordLists->spellsList;
			if (spell == spells_list->head) {
				spell->nextRecord->prevRecord = NULL;
				spells_list->head = spell->nextRecord;
			}
			else if (spell == spells_list->tail) {
				spell->prevRecord->nextRecord = NULL;
				spells_list->tail = spell->prevRecord;
			}
			else {
				SPELRecord* spell_next = spell->nextRecord;
				SPELRecord* spell_prev = spell->prevRecord;
				spell_next->prevRecord = spell_prev;
				spell_prev->nextRecord = spell_next;
			}
			spells_list->size--;
			machine.Free(spell->friendlyName);
			spell->friendlyName = NULL;
			machine.Free(spell->id);
			spell->id = NULL;
			machine.Free(spell);
			result = 1;
		}
	}
	return machine.push(result);
}

bool FUNCCREATESPELL::execute(void)
{
	VMLONG spell_id, spell_name;
	VMLONG result = 0;
	if (machine.pop(spell_id) && machine.pop(spell_name)) {
		char const* id = 
			machine.GetString(reinterpret_cast<VPVOID>(spell_id));
		if (strlen(id) <= 31 && !GetSpellRecord(spell_id, machine)) {
			TES3CELLMASTER* cell_master =
				*(reinterpret_cast<TES3CELLMASTER**>reltolinear(MASTERCELL_IMAGE));
			LinkedList* spells_list = cell_master->recordLists->spellsList;
			SPELRecord* tail_spell = 
				static_cast<SPELRecord*>(spells_list->tail);
			SPELRecord* new_spell = 
				static_cast<SPELRecord*>(machine.Malloc(sizeof(SPELRecord)));
			memset(new_spell, 0, sizeof(*new_spell));
			new_spell->vTable = tail_spell->vTable;
			new_spell->recordType = SPELL;
			new_spell->spellsList = spells_list;
			new_spell->id = static_cast<char*>(machine.Malloc(strlen(id) + 1));
			strcpy(new_spell->id, id);
			char const* new_name = 
				machine.GetString(reinterpret_cast<VPVOID>(spell_name));
			if (strlen(new_name) > 31) {
				new_spell->friendlyName = static_cast<char*>(machine.Malloc(32));
				strncpy(new_spell->friendlyName, new_name, 31);
				new_spell->friendlyName[31] = '\0';
			}
			else {
				new_spell->friendlyName = 
					static_cast<char*>(machine.Malloc(strlen(new_name) + 1));
				strcpy(new_spell->friendlyName, new_name);
			}
			for (int i = 1; i < 8; ++i) {
				new_spell->effects[i].effectId = kNoEffect;
			}
			SetEffect(new_spell->effects, 1, kWaterBreathing, kNoSkill, kSelf, 0, 1, 0, 0);
			new_spell->cost = 1;
			result = 1;

			// Call Morrowind's native add object function
			int const kAddObject = 0x4B8980;
			__asm {
				mov edx, dword ptr ds:[0x7C67E0];
				mov ecx, dword ptr ds:[edx];
				push new_spell;
				call kAddObject;
			}
		}
	}
	return machine.push(result);
}

bool FUNCSETBASEEFFECTINFO::execute(void)
{
	VMLONG effect_id, school, flags;
	VMFLOAT base_magicka_cost;
	VMLONG result = 0;
	if (machine.pop(effect_id) && machine.pop(school) &&
		machine.pop(base_magicka_cost) && machine.pop(flags) && 
		kFirstMagicEffect <= effect_id && effect_id <= kLastMagicEffect &&
		kFirstMagicSchool <= school && school <= kLastMagicSchool) {
		TES3CELLMASTER* cell_master =
			*(reinterpret_cast<TES3CELLMASTER**>reltolinear(MASTERCELL_IMAGE));
		MGEFRecord& effect =
			cell_master->recordLists->magic_effects[effect_id];
		effect.school = school;
		effect.base_magicka_cost = base_magicka_cost;
		effect.flags = (flags & (kSpellmaking | kEnchanting | kNegativeLighting));
		result = 1;
	}
	return machine.push(result);
}

bool FUNCGETBASEEFFECTINFO::execute(void)
{
	VMLONG effect_id;
	VMLONG school = -1;
	VMFLOAT base_magicka_cost = 0.0;
	VMLONG flags = 0;
	if (machine.pop(effect_id) &&
		kFirstMagicEffect <= effect_id && effect_id <= kLastMagicEffect) {
		TES3CELLMASTER* cell_master =
			*(reinterpret_cast<TES3CELLMASTER**>reltolinear(MASTERCELL_IMAGE));
		MGEFRecord const& effect =
			cell_master->recordLists->magic_effects[effect_id];
		school = effect.school;
		base_magicka_cost = effect.base_magicka_cost;
		flags = effect.flags | kMagicEffectFlags[effect_id];
	}
	return machine.push(flags) && machine.push(base_magicka_cost) &&
		machine.push(school);
}

bool FUNCGETMAGIC::execute(void)
{
	VMLONG id = 0;
	VMLONG type = 0;
	MACPRecord* macp = machine.GetMacpRecord();
	
	if (macp)
	{
		BaseRecord const * const rec = static_cast<BaseRecord const * const>(macp->currentSpell);
		if (rec)
		{
			type = rec->recordType;
			if (type == SPELL)
			{
				SPELRecord const * const spell = reinterpret_cast<SPELRecord const * const>(rec);
				id = reinterpret_cast<VMLONG>(strings.add(spell->id));
			}
			else if (type == ENCHANTMENT)
			{
				ENCHRecord const * const enchantment = reinterpret_cast<ENCHRecord const * const>(rec);
				id = reinterpret_cast<VMLONG>(strings.add(enchantment->id));
			}
		}
	}

	return (machine.push(id) && machine.push(type));
}

bool FUNCGETPROGRESSSKILL::execute(void)
{
	VMLONG skillIndex;
	VMFLOAT progress = -1.0;
	VMFLOAT normalized = -1.0;
	MACPRecord* macp = machine.GetMacpRecord();

	if (macp &&
		machine.pop(skillIndex) && skillIndex >= kFirstSkill && skillIndex <= kLastSkill)
	{
		progress = macp->skillProgress[skillIndex];
		normalized = 100 * progress / GetSkillRequirement(machine, static_cast<Skills>(skillIndex));
	}

#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETPROGRESSSKILL()\n",progress);
#endif	
	return (machine.push(normalized) && machine.push(progress));
}

bool FUNCSETPROGRESSSKILL::execute(void)
{
	VMLONG skillIndex;
	VMLONG normalized;
	VMFLOAT progress;
	bool result = false;
	MACPRecord* macp = machine.GetMacpRecord();
	
	if (macp && 
		machine.pop(skillIndex) && skillIndex >= kFirstSkill && skillIndex <= kLastSkill &&
		machine.pop(progress) && progress >= 0 &&
		machine.pop(normalized))
	{
		if (normalized)
		{
			progress = GetSkillRequirement(machine, static_cast<Skills>(skillIndex)) * progress / 100;
		}
		macp->skillProgress[skillIndex] = progress;
		machine.CheckForSkillUp(skillIndex);
		result = true;
	}

#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCSETPROGRESSSKILL()\n",progress);
#endif	
	return (machine.push(static_cast<VMREGTYPE>(result)));
}

bool FUNCMODPROGRESSSKILL::execute(void)
{
	VMLONG skillIndex;
	VMLONG normalized;
	VMFLOAT mod;
	bool result = false;
	MACPRecord* macp = machine.GetMacpRecord();
	
	if (macp && 
		machine.pop(skillIndex) && skillIndex >= kFirstSkill && skillIndex <= kLastSkill &&
		machine.pop(mod) &&
		machine.pop(normalized))
	{
		float progress = macp->skillProgress[skillIndex];
		if (normalized)
		{
			float const requirement = GetSkillRequirement(machine, static_cast<Skills>(skillIndex));
			
			// normalize progress, then add mod, then convert back.
			// this avoids some floating point precision errors.
			progress = 100 * progress /requirement;
			progress += mod;
			progress = requirement * progress / 100;
		}
		else
		{
			progress += mod;
		}
		if (progress < 0)
			progress = 0.0;
		macp->skillProgress[skillIndex] = progress;
		machine.CheckForSkillUp(skillIndex);
		result = true;
	}

#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCMODPROGRESSSKILL()\n",progress);
#endif	
	return (machine.push(static_cast<VMREGTYPE>(result)));
}

bool FUNCGETBASESKILL::execute(void)
{
	long skill_index;
	float value = -1.0;
	unsigned long type;
	VPVOID refr, temp;
	if (GetTargetData(machine, &refr, &temp, &type) && type == NPC) {
		MACPRecord* macp = 
			reinterpret_cast<MACPRecord*>(GetAttachPointer(machine, refr, MACHNODE));
		if (macp && machine.pop(skill_index) && skill_index >= kFirstSkill
			&& skill_index <= kLastSkill) {
			value = macp->skills[skill_index].base;
		}
	}
	return (machine.push(value));
}

bool FUNCGETBASEACROBATICS::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;

	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x13d, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEACR()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEALCHEMY::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x12d, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEALCHEMY()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEALTERATION::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x119, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEALTERATION()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEARMORER::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0xf1, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEARMORER()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEATHLETICS::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x10d, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEATHLETICS()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEAXE::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x105, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASE()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEBLOCK::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0xed, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEBLOCK()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEBLUNTWEAPON::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0xfd, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEBLUNTWEAPON()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASECONJURATION::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x121, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASECONJURATION()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEDESTRUCTION::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x115, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEDESTRUCTION()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEENCHANT::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x111, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEENCHANT()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEHANDTOHAND::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x155, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEHANDTOHAND()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEHEAVYARMOR::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0xf9, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEHEAVYARMOR()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEILLUSION::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x11d, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEILLUSION()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASELIGHTARMOR::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x141, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASELIGHTARMOR()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASELONGBLADE::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x101, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASELONGBLADE()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEMARKSMAN::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x149, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEMARKSMAN()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEMEDIUMARMOR::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0xf5, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEMEDIUMARMOR()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEMERCANTILE::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x14d, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEMERCANTILE()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEMYSTICISM::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x125, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEMYSTICISM()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASERESTORATION::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x129, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASERESTORATION()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASESECURITY::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x135, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASESECURITY()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASESHORTBLADE::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x145, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASESHORTBLADE()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASESNEAK::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x139, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASESNEAK()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASESPEAR::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x109, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASESPEAR()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASESPEECHCRAFT::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x151, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASESPEECHCRAFT()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETBASEUNARMORED::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x131, value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEUNARMORED()\n",value);
#endif	
	return machine.push(value);
}

bool FUNCGETPROGRESSLEVEL::execute(void)
{
	VMLONG progress = -1;
	MACPRecord* macp = machine.GetMacpRecord();
	
	if (macp)
	{
		progress = macp->levelProgress;
	}

#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETPROGRESSLEVEL()\n",progress);
#endif	

	return (machine.push(progress));
}

bool FUNCSETPROGRESSLEVEL::execute(void)
{
	VMLONG progress = 0;
	bool result = false;
	
	MACPRecord* macp = machine.GetMacpRecord();
	
	if (macp &&
		machine.pop(progress) && progress >= 0)
	{
		macp->levelProgress = progress;
		CheckForLevelUp(progress);
		result = true;
	}

#ifdef DEBUGGING
    cLog::mLogMessage("%d:FUNCSETPROGRESSLEVEL(%d)\n",result,progress);
#endif	

	return machine.push(static_cast<VMREGTYPE>(result));
}

bool FUNCMODPROGRESSLEVEL::execute(void)
{
	VMLONG mod = 0;
	bool result = false;
	
	MACPRecord* macp = machine.GetMacpRecord();
	
	if (macp &&
		machine.pop(mod))
	{
		long progress = mod + macp->levelProgress;
		if (progress < 0)
			progress = 0;
		macp->levelProgress = progress;
		CheckForLevelUp(progress);
		result = true;
	}

#ifdef DEBUGGING
    cLog::mLogMessage("%d:FUNCMODPROGRESSLEVEL(%d)\n",result,progress);
#endif	

	return machine.push(static_cast<VMREGTYPE>(result));
}


bool FUNCGETLOCKLEVEL::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	VMSHORT lockLevel = -1;
	
	if (GetTargetData(machine, &refr, &temp, &type))
	{
		if (type == CONTAINER || type == DOOR)
		{
			TES3LOCK lockInfo;
			if (GetAttachData(machine, refr, 3, 0, lockInfo))
			{
				lockLevel = lockInfo.lockLevel;
			}
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETLOCKLEVEL()\n",lockLevel);
#endif	
	return machine.push(static_cast<VMREGTYPE>(lockLevel));
}

bool FUNCGETTRAP::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	VMLONG trapId = 0;
	VMLONG trapName = 0;
	VMSHORT trapCost = 0;

	if (GetTargetData(machine, &refr, &temp, &type))
	{
		if (type == CONTAINER || type == DOOR)
		{
			TES3LOCK lockInfo;
			if (GetAttachData(machine, refr, 3, 0, lockInfo))
			{
				SPELRecord * trapSpell = lockInfo.trapSpell;
				if (trapSpell != 0)
				{
					trapId = reinterpret_cast<VMLONG>(strings.add(trapSpell->id));
					trapName = reinterpret_cast<VMLONG>(strings.add(trapSpell->friendlyName));
					trapCost = trapSpell->cost;
				}
			}
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETTRAP()\n",trapId);
#endif	
	return machine.push(static_cast<VMREGTYPE>(trapCost)) && machine.push(trapName) && machine.push(trapId);
}

bool FUNCSETTRAP::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	VMLONG spellId;
	bool success = false;
		
	if (GetTargetData(machine, &refr, &temp, &type) && machine.pop(spellId))
	{
		if (type == CONTAINER || type == DOOR)
		{
			TES3LOCK lockInfo;
			if (GetAttachData(machine, refr, 3, 0, lockInfo))
			{
				SPELRecord * newTrap = GetSpellRecord(spellId, machine);			
				
				// don't update if we received an unknown spell id
				if (spellId == 0 || newTrap != 0)
				{
					lockInfo.trapSpell = newTrap;
					success = SetAttachData(machine, refr, 3, 0, lockInfo);
				}
			}
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCSETTRAP()\n",success);
#endif	
	return machine.push(static_cast<VMREGTYPE>(success));
}

bool FUNCSPELLLIST::execute(void)
{
	LinkedListNode* next = 0;
	VMLONG total_spells = 0;
	VMLONG spellid = 0;
	VMLONG name = 0;
	VMLONG type = 0;
	VMLONG cost = 0;
	VMLONG effects = 0;
	VMLONG flags = 0;
	VPVOID refr, temp;
	unsigned long ref_type;
	if (machine.pop(reinterpret_cast<VMREGTYPE&>(next)) && GetTargetData(machine, &refr, &temp, &ref_type)) {
		if (ref_type == NPC) {
			TES3REFERENCE * npcRef = reinterpret_cast<TES3REFERENCE*>(refr);
			NPCCopyRecord * npcCopy = reinterpret_cast<NPCCopyRecord*>(npcRef->templ);
			NPCBaseRecord * npcRec = npcCopy->baseNPC;
			total_spells = npcRec->numberOfSpells;
			if (total_spells > 0) {
				if (!next) {
					next = npcRec->spellStart;
				}
				SPELRecord * spell = reinterpret_cast<SPELRecord*>(next->dataNode);
				next = next->nextNode;
				spellid = reinterpret_cast<VMLONG>(strings.add(spell->id));
				name = reinterpret_cast<VMLONG>(strings.add(spell->friendlyName));
				type = spell->type;
				cost = spell->cost;
				effects = CountEffects(spell->effects);
				flags = spell->flags;
			}
		}
	}

	return machine.push(reinterpret_cast<VMREGTYPE>(next)) &&
		machine.push(flags) && machine.push(effects) &&
		machine.push(cost) && machine.push(type) && machine.push(name) &&
		machine.push(spellid) && machine.push(total_spells);
}

bool FUNCGETSPELL::execute(void)
{
	VMLONG spellId = 0;
	VMSHORT result = 0;
	VPVOID refr, temp;
	unsigned long refType;

	if (machine.pop(spellId) && GetTargetData(machine, &refr, &temp, &refType))
	{
		if (refType == NPC && spellId != 0)
		{
			char const * idString = machine.GetString(reinterpret_cast<VPVOID>(spellId));
			TES3REFERENCE * npcRef = reinterpret_cast<TES3REFERENCE*>(refr);
			NPCCopyRecord * npcCopy = reinterpret_cast<NPCCopyRecord*>(npcRef->templ);
			LinkedListNode * curr = npcCopy->baseNPC->spellStart;
			while (curr != 0)
			{
				SPELRecord * spell = reinterpret_cast<SPELRecord*>(curr->dataNode);
				if (strcmp(idString, spell->id) == 0)
				{
					result = 1;
					break;
				}
				curr = curr->nextNode;
			}
		}
	}

	return machine.push(static_cast<VMREGTYPE>(result));
}

bool FUNCSETSPELLINFO::execute(void)
{
	VMLONG spellid, name, type, cost, flags, origin;
	VMLONG result = 0;
	if (machine.pop(spellid) &&
		machine.pop(name) &&
		machine.pop(type) &&
		machine.pop(cost) &&
		machine.pop(flags) &&
		machine.pop(origin) &&
		kFirstSpellTypes <= type && type <= kLastSpellTypes &&
		kNoSpellFlags <= flags && flags <= kAllSpellFlags &&
		(origin == 0 || (kSpellOriginsFirst <= origin && origin <= kSpellOriginsLast))) {
		SPELRecord* spell = GetSpellRecord(spellid, machine);
		if (spell) {
			char const* newName = machine.GetString(reinterpret_cast<VPVOID>(name));
			if (newName) {
				if (strlen(newName) > strlen(spell->friendlyName)) {
					// The CS limits spell names to 31 characters, so we will too.
					// TODO Is it safe to realloc? We can't be sure that
					// nothing else knows about this pointer.
					spell->friendlyName =
						static_cast<char*>(machine.Realloc(spell->friendlyName, 32));
					strncpy(spell->friendlyName, newName, 31);
					spell->friendlyName[31] = '\0';
				}
				else {
					strcpy(spell->friendlyName, newName);
				}
			}
			if (flags & kAutoCalculateCost &&
				!(spell->flags & kAutoCalculateCost)) {
				// TODO Need to find the native function(s) that calculate
				// spell cost.
			}
			else if (!(flags & kAutoCalculateCost)) {
				spell->cost = cost;
			}
			spell->type = type;
			spell->flags = flags;
			if (origin != 0) {
				spell->origin = origin;
			}
			result = 1;
		}
	}
	return machine.push(result);
}

bool FUNCGETSPELLINFO::execute(void)
{
	VMLONG spellid;
	VMLONG name = 0;
	VMLONG type = 0;
	VMLONG cost = 0;
	VMLONG effects = 0;
	VMLONG flags = 0;
	VMLONG origin = 0;

	if (machine.pop(spellid)) {
		SPELRecord * spell = GetSpellRecord(spellid, machine);
		if (spell) {
			name = reinterpret_cast<VMLONG>(strings.add(spell->friendlyName));
			type = spell->type;
			cost = spell->cost;
			effects = CountEffects(spell->effects);
			flags = spell->flags;
			origin = spell->origin;
		}
	}
	return machine.push(origin) && machine.push(flags) && 
		machine.push(effects) && machine.push(cost) && 
		machine.push(type) && machine.push(name);
}

bool FUNCDELETEEFFECT::execute(void)
{
	VMLONG type, id, effectIndex;
	VMLONG result = 0;

	if (machine.pop(type) &&
		machine.pop(id) &&
		machine.pop(effectIndex) && 
		1 <= effectIndex && effectIndex <= 8)
	{
		Effect * effects = GetEffects(type, id, machine);
		if (effects)
		{
			short numEffects = CountEffects(effects);
			--effectIndex; // 0-based array index
			if (numEffects > 1 && effectIndex < numEffects)
			{
				result = 1;
				effects[effectIndex].effectId = kNoEffect;
				for (int i = effectIndex + 1; i < numEffects; ++i)
				{
					effects[i-1] = effects[i];
					effects[i].effectId = kNoEffect;
				}
			}
		}
	}
	return machine.push(result);
}

bool FUNCSETEFFECTINFO::execute(void)
{
	VMLONG type, id, effect_index, effect_id, skill_attribute_id,
		range, area, duration, mininum_magnitude, maximum_magnitude;
	VMLONG result = 0;
	if (machine.pop(type) && machine.pop(id) && machine.pop(effect_index) &&
		machine.pop(effect_id) && machine.pop(skill_attribute_id) &&
		machine.pop(range) && machine.pop(area) && machine.pop(duration) &&
		machine.pop(mininum_magnitude) && machine.pop(maximum_magnitude)) {
		Effect * effects = GetEffects(type, id, machine);
		result = SetEffect(effects, effect_index, effect_id, 
			skill_attribute_id, range, area, duration, mininum_magnitude,
			maximum_magnitude);
	}
	return machine.push(result);
}

bool FUNCADDEFFECT::execute(void)
{
	VMLONG type, id, effect_id, skill_attribute_id,
		range, area, duration, minimum_magnitude, maximum_magnitude;
	VMLONG result = 0;
	if (machine.pop(type) && machine.pop(id) && machine.pop(effect_id) &&
		machine.pop(skill_attribute_id) && machine.pop(range) &&
		machine.pop(area) && machine.pop(duration) &&
		machine.pop(minimum_magnitude) && machine.pop(maximum_magnitude))
	{
		Effect* effects = GetEffects(type, id, machine);
		short num_effects = CountEffects(effects);
		if (num_effects < 8) {
			result = SetEffect(effects, num_effects + 1, effect_id,
				skill_attribute_id, range, area, duration, minimum_magnitude,
				maximum_magnitude);
		}
	}
	return machine.push(result);
}

bool FUNCGETEFFECTINFO::execute(void)
{
	VMLONG type, id, effectIndex;
	VMLONG effect_id = kNoEffect;
	VMLONG skill_attribute_id = 0;
	VMLONG rangeType = 0;
	VMLONG area = 0;
	VMLONG duration = 0;
	VMLONG magMin = 0;
	VMLONG magMax = 0;

	if (machine.pop(type) &&
		machine.pop(id) && 
		machine.pop(effectIndex) &&
		1 <= effectIndex && effectIndex <= 8)
	{
		Effect const * effects = GetEffects(type, id, machine);
		if (effects)
		{
			--effectIndex; // 0-based array index
			Effect const & effect = effects[effectIndex];
			if (effect.effectId != kNoEffect) {
				effect_id = effect.effectId;
				int effect_flags = kMagicEffectFlags[effect_id];
				if (effect_flags & kTargetSkill) {
					skill_attribute_id = effect.skillId;
				}
				else if (effect_flags & kTargetAttribute) {
					skill_attribute_id = effect.AttributeId;
				}
				else {
					skill_attribute_id = kNoSkill;
				}
				rangeType = effect.RangeType;
				area = effect.Area;
				duration = effect.Duration;
				magMin = effect.MagMin;
				magMax = effect.MagMax;
			}
		}
	}

#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETSPELLINFO()\n",spellId);
#endif	
	return machine.push(magMax) && machine.push(magMin) &&
		machine.push(duration) && machine.push(area) &&
		machine.push(rangeType) && machine.push(skill_attribute_id) &&
		machine.push(effect_id);
}

bool FUNCGETSPELLEFFECTINFO::execute(void)
{
	VMLONG spellId;
	VMLONG effectIndex;
	VMSHORT effectId = -1;
	VMSHORT skillId = 0;
	VMSHORT attributeId = 0;
	VMLONG rangeType = 0;
	VMLONG area = 0;
	VMLONG duration = 0;
	VMLONG magMin = 0;
	VMLONG magMax = 0;

	if (machine.pop(spellId) && machine.pop(effectIndex)
		&& 1 <= effectIndex && effectIndex <= 8)
	{
		SPELRecord * spell = GetSpellRecord(spellId, machine);
		--effectIndex; // 0-based array index
		if (spell != 0 && spell->effects[effectIndex].effectId != kNoEffect)
		{			
			effectId = spell->effects[effectIndex].effectId;
			skillId = spell->effects[effectIndex].skillId;
			attributeId = spell->effects[effectIndex].AttributeId;
			rangeType = spell->effects[effectIndex].RangeType;
			area = spell->effects[effectIndex].Area;
			duration = spell->effects[effectIndex].Duration;
			magMin = spell->effects[effectIndex].MagMin;
			magMax = spell->effects[effectIndex].MagMax;
		}
	}

#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETSPELLINFO()\n",spellId);
#endif	
	return machine.push(magMax) && machine.push(magMin) && machine.push(duration) && machine.push(area) && machine.push(rangeType) 
		&& machine.push(static_cast<VMREGTYPE>(attributeId)) && machine.push(static_cast<VMREGTYPE>(skillId)) 
		&& machine.push(static_cast<VMREGTYPE>(effectId));
}

bool FUNCGETENCHANT::execute(void)
{
	long enchId = 0;
	long type = 0;
	long cost = 0;
	float currCharge = 0;
	long maxCharge = 0;
	long effects = 0;
	long autocalc = 0;
	VPVOID refr, temp;
	unsigned long refType;
	if (GetTargetData(machine, &refr, &temp, &refType))	{
		if (refType == WEAPON || refType == ARMOR || refType == CLOTHING 
			|| refType == BOOK) {
			TES3REFERENCE* itemRef = reinterpret_cast<TES3REFERENCE*>(refr);
			ENCHRecord* ench = 0;
			if (refType == WEAPON) {
				WEAPRecord* weapon = reinterpret_cast<WEAPRecord*>(itemRef->templ);
				ench = weapon->enchantment;
			}
			else if (refType == ARMOR) {
				ARMORecord* armor = reinterpret_cast<ARMORecord*>(itemRef->templ);
				ench = armor->enchantment;			
			}
			else if (refType == CLOTHING) {
				CLOTRecord* clothing = reinterpret_cast<CLOTRecord*>(itemRef->templ);
				ench = clothing->enchantment;
			}
			else if (refType == BOOK) {
				BOOKRecord* book = reinterpret_cast<BOOKRecord*>(itemRef->templ);
				if (book->scroll == 1) {
					ench = book->enchantment;
				}
			}
			if (ench) {
				enchId = reinterpret_cast<VMLONG>(strings.add(ench->id));
				type = ench->type;
				cost = ench->cost;
				maxCharge = ench->charge;
				effects = CountEffects(ench->effects);
				autocalc = ench->autocalc;
				// get the current charge
				if (!GetAttachData(machine, refr, VARNODE, 4, currCharge)) {
					currCharge = maxCharge;
				}
			}
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETENCHANT()\n",0);
#endif	
	return machine.push(autocalc) && machine.push(effects) && machine.push(maxCharge)
		&& machine.push(currCharge) && machine.push(cost)
		&& machine.push(type) && machine.push(enchId);
}

bool FUNCSETENCHANTINFO::execute(void)
{
	VMLONG enchId, type, cost, charge, autocalc;
	VMLONG result = 0;

	if (machine.pop(enchId) &&
		machine.pop(type) &&
		machine.pop(cost) &&
		machine.pop(charge) &&
		machine.pop(autocalc) &&
		0 <= type && type <= 3 &&
		0 <= autocalc && autocalc <= 1)
	{
		ENCHRecord * ench = GetEnchantmentRecord(enchId, machine);
		if (ench)
		{
			result = 1;
			ench->type = type;
			ench->cost = cost;
			ench->charge = charge;
			ench->autocalc = autocalc;
		}
	}

	return machine.push(result);
}

bool FUNCGETENCHANTINFO::execute(void)
{
	VMLONG enchId;
	VMSHORT type = 0;
	VMSHORT cost = 0;
	VMLONG maxCharge = 0;
	VMSHORT effects = 0;
	VMLONG autocalc = 0;

	if (machine.pop(enchId))
	{
		ENCHRecord * ench = GetEnchantmentRecord(enchId, machine);
		if (ench != 0)
		{
			type = ench->type;
			cost = ench->cost;
			maxCharge = ench->charge;
			effects = CountEffects(ench->effects);
			autocalc = ench->autocalc;
		}
	}

#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETENCHANTINFO()\n",spellId);
#endif	
	return machine.push(autocalc) && machine.push(static_cast<VMREGTYPE>(effects)) && machine.push(maxCharge)
		&& machine.push(static_cast<VMREGTYPE>(cost)) && machine.push(static_cast<VMREGTYPE>(type));
}

bool FUNCGETENCHANTEFFECTINFO::execute(void)
{
	VMLONG enchId;
	VMLONG effectIndex;
	VMSHORT effectId = -1;
	VMLONG rangeType;
	VMLONG area;
	VMLONG duration;
	VMLONG magMin;
	VMLONG magMax;

	if (machine.pop(enchId) && machine.pop(effectIndex)
		&& 1 <= effectIndex && effectIndex <= 8)
	{
		ENCHRecord * ench = GetEnchantmentRecord(enchId, machine);
		--effectIndex; // 0-based array index
		if (ench != 0 && ench->effects[effectIndex].effectId != kNoEffect)
		{
			effectId = ench->effects[effectIndex].effectId;
			rangeType = ench->effects[effectIndex].RangeType;
			area = ench->effects[effectIndex].Area;
			duration = ench->effects[effectIndex].Duration;
			magMin = ench->effects[effectIndex].MagMin;
			magMax = ench->effects[effectIndex].MagMax;
		}
	}

#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETENCHANTEFFECTINFO()\n",spellId);
#endif	
	return machine.push(magMax) && machine.push(magMin) && machine.push(duration) && machine.push(area) && machine.push(rangeType) 
		&& machine.push(static_cast<VMREGTYPE>(effectId));
}

bool FUNCGETCLASS::execute(void)
{
	VMLONG attributeMask;
	VMLONG majorMask;
	VMLONG minorMask;

	VMLONG id = 0;
	VMLONG name = 0;
	VMLONG specialization = 0;
	VMLONG attributes = 0;
	VMLONG majorSkills = 0;
	VMLONG minorSkills = 0;
	VMLONG playable = 0;

	VPVOID refr, temp;
	unsigned long refType;

	if (machine.pop(attributeMask) && machine.pop(majorMask) && machine.pop(minorMask)
		&& GetTargetData(machine, &refr, &temp, &refType) && refType == NPC)
	{
		TES3REFERENCE * npcRef = reinterpret_cast<TES3REFERENCE*>(refr);
		NPCCopyRecord * npcCopy = reinterpret_cast<NPCCopyRecord*>(npcRef->templ);
		CLASRecord * charClass = npcCopy->baseNPC->characterClass;
		
		id = reinterpret_cast<VMLONG>(strings.add(charClass->id));
		name = reinterpret_cast<VMLONG>(strings.add(charClass->name));

		playable = charClass->playable;
		specialization = charClass->specialization;
		
		attributes = (1 << charClass->attributes[0]) + (1 << charClass->attributes[1]);

		if (attributeMask != 0)
		{
			attributes &= attributeMask;
		}

		for (int i = 0; i < 10; i += 2)
		{
			minorSkills += 1 << charClass->skills[i];
			majorSkills += 1 << charClass->skills[i + 1];
		}

		if (minorMask != 0)
		{
			minorSkills &= minorMask;
		}
		if (majorMask != 0)
		{
			majorSkills &= majorMask;
		}
	}

#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETCLASS()\n",name);
#endif	

	return machine.push(minorSkills) && machine.push(majorSkills) && machine.push(attributes) 
		&& machine.push(specialization) && machine.push(playable) && machine.push(name) && machine.push(id);
}

// GRM 15 Jan 2007
// Set value (worth in gold) of reference.
// <ref> xSetValue <long> -> [no value]
bool FUNCSETVALUE::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	ULONG offset = 0;
	VMREGTYPE value = 0;
    bool set = false;
	char idstr[129];
    idstr[sizeof idstr-1] = '\0';

	if (GetTargetData(machine, &refr, &temp, &type) && machine.pop(value))
	{
		offset = offsetOfValue(type);
        // MISC is possibly gold.
        bool isGold = type == MISC &&
            GetIdString(machine, temp, idstr) &&
            strncmp(idstr, "gold_", 5) == 0;
        if (offset != 0 && !isGold) {
            if (type == CLOTHING) {
                // Clothing is a short. Since SetOffsetData only stores
                // longs, we need to fetch the value, and modify the
                // lower 16 bits.
                unsigned long oldValue;
                if (GetOffsetData(machine, temp, offset, &oldValue)) {
                    value = (oldValue & ~0xFFFF) | (value & 0xFFFF);
                }
            }
            // Note: count is ignored.
            set = SetOffsetData(machine, temp, offset, value);
        }
	}
#ifdef DEBUGGING
    cLog::mLogMessage("%d:FUNCSETVALUE(%d)\n",set,value);
#endif	
	return machine.push(static_cast<VMREGTYPE>(set));
}

// Set current charge of item, if possible
//  ref xSetCharge <float> -> [no value]
bool FUNCSETCHARGE::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	VMFLOAT charge = 0;
    bool set = false;

    if (GetTargetData(machine, &refr, &temp, &type) && machine.pop(charge))
	{
        // Set charge in attached node. If not present, cannot
        // set current charge.
        set = SetAttachData(machine, refr, VARNODE, 4, min(charge,
                                                               GetMaxCharge(machine, temp, type)));
	}
#ifdef DEBUGGING
	cLog::mLogMessage("FUNCSETCHARGE(%d)\n",charge);
#endif
	return machine.push(static_cast<VMREGTYPE>(set));
}

// Set current maximum charge of item, if possible
//  ref xSetCharge <float> -> [no value]
bool FUNCSETMAXCHARGE::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	VMFLOAT maxcharge = 0;
    bool set = false;

	if (GetTargetData(machine, &refr, &temp, &type) && machine.pop(maxcharge))
	{
        VPVOID offset = GetMaxChargeOffset(machine, temp, type);
        if (offset) {
            VMFLOAT charge;
            // If the new max charge is less than the current charge,
            // force it to match.
            if (GetAttachData(machine, refr, VARNODE, 4, charge) &&
                charge >= maxcharge) {
                SetAttachData(machine, refr, VARNODE, 4, maxcharge);
            }
            // actually stored as integer
			set = SetOffsetData(machine,offset,0xc,static_cast<int>(maxcharge));
		}
	}

#ifdef DEBUGGING
	cLog::mLogMessage("FUNCSETMAXCHARGE(%d)\n",maxcharge);
#endif
	return machine.push(static_cast<VMREGTYPE>(set));
}

// GRM 17 Jan 2007
// <ref> xSetCondition <long> -> [no value]
bool FUNCSETCONDITION::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	VMREGTYPE value = 0;
    bool set = false;

    if (GetTargetData(machine, &refr, &temp, &type) && machine.pop(value)
        && (type == WEAPON || type == ARMOR))
	{
        // Condition is only stored as attached data. (Note that
        // GetCondition returns the maximum condition if the
        // attached data is not found).
        //
        // TODO: If the attached data is not found, it should be
        // created.
        set = SetAttachData(machine, refr, VARNODE, 3, min(static_cast<ULONG>(value),
            GetMaxCondition(machine, temp, type)));
	}
	
#ifdef DEBUGGING
	cLog::mLogMessage("FUNCSETCONDITION(%d)\n",value);
#endif	
	return machine.push(static_cast<VMREGTYPE>(set));
}
// GRM 17 Jan 2007
// <ref> xSetMaxCondition <long> -> [no value]
bool FUNCSETMAXCONDITION::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	VMREGTYPE value = 0;
    bool set = false;

	if (GetTargetData(machine, &refr, &temp, &type) && machine.pop(value)
        && (type == WEAPON || type == ARMOR))
	{
		ULONG offset = offsetOfCondition(type); // misnomer, actually MaxCondition
        if ( offset ) {
            // If the new max condition is less than the current condition,
            // force it to match.
            VMREGTYPE condition;
            if (GetAttachData(machine, refr, VARNODE, 3, condition) &&
                condition > value) {
                    SetAttachData(machine, refr, VARNODE, 3, value);
            }

            if ( type == WEAPON) {
                // Weapon uses upper 16 bits of value
                ULONG oldValue;
                GetOffsetData(machine, temp, offset, &oldValue);
                value = (value << 16) | (oldValue & 0xFFFF);
            }

			set = SetOffsetData(machine, temp, offset, value);
        }
	}
#ifdef DEBUGGING
	cLog::mLogMessage("FUNCSETMAXCONDITION(%d)\n",value);
#endif	
	return machine.push(static_cast<VMREGTYPE>(set));
}

// GRM 15 Jan 2007
//  Common code for SETxxx functions.
//  ref xSetXxxxx <long|float> -> [no value]
bool CommonSet::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	ULONG offset = 0;
	VMFLOAT value = 0;
    bool set = false;

    if (GetTargetData(machine, &refr, &temp, &type) && machine.pop(value) &&
        canSet(type))
	{
		offset = getTypeOffset(type);
        if (offset != 0) {
            // Note: count is ignored.
            set = SetOffsetData(machine, temp, offset, value);
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%s(%f)\n", name, value);
#endif	
	return machine.push(static_cast<VMREGTYPE>(set));
}

bool FUNCGETSPELLEFFECTS::execute(void)
{
	bool result= true;
	VMREGTYPE pString= 0;
	const char* string= "null";
	VMREGTYPE count= 0;

	if(machine.pop(pString) && (string=machine.GetString((VPVOID)pString))!=0)
	{
		VMLONG strlength= strlen((const char*)string);
		parent= machine.GetFlow();
		result= machine.WriteMem((VPVOID)reltolinear(SECONDOBJECT_LENGTH_IMAGE),&strlength,sizeof(strlength))
			&& machine.WriteMem((VPVOID)reltolinear(SECONDOBJECT_IMAGE),(void*)string,strlength+1);
		if(result)
		{
			Context context= machine.GetFlow();
			context.Eip= (DWORD)reltolinear(FIXUPTEMPLATE);
			machine.SetFlow(context);
			result= machine.SetVMDebuggerBreakpoint(this);
		}
	}
	else
		result= false;

	return result;
}

BYTE FUNCGETSPELLEFFECTS::getid()
{
	return BP_FIXUPTEMPLATE;
}

bool FUNCGETSPELLEFFECTS::breakpoint()
{
	bool result= false;
	Context flow= machine.GetFlow();
	if(machine.WriteMem((VPVOID)reltolinear(SECONDOBJECT_IMAGE),&flow.Eax,sizeof(flow.Eax)))
	{
		machine.SetFlow(parent);
		result= CallOriginalFunction(machine,ORIG_GETSPELLEFFECTS);
	}
	
#ifdef DEBUGGING
	cLog::mLogMessage("FUNCGETSPELLEFFECTSb() %s\n",result?"succeeded":"failed");
#endif

	return result;
}

bool FUNCGETVALUE::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	ULONG offset = 0;
	ULONG count = 0;
	ULONG value = 0;
	char idstr[129];
	idstr[128] = 0;
	if (GetTargetData(machine, &refr, &temp, &type))
	{
		offset = offsetOfValue(type);
		if (offset && GetOffsetData(machine, temp, offset, &value))
		{
			// clothing value is stored in a short not long like all others
			if (type == CLOTHING)
				value %= 65536;
	
			// gold is special (it's always worth one, but not in the CS)
			if ( type == MISC && GetIdString(machine, temp, idstr))
				if (!strncmp(idstr,"Gold_",5 )) 
					value = 1;
	
			if (GetAttachData(machine, refr, VARNODE, 0, &count))
				value *= count;
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCGETVALUE()\n",value);
#endif	
	return machine.push((VMREGTYPE)value);
}

bool FUNCGETOWNER::execute(void)
{
	VPVOID refr;
	ULONG data;
	ULONG id = 0;
	char idstr[129];
	idstr[128] = 0;
	if (GetTargetData(machine, &refr))
	{
		if (GetAttachData(machine, refr, VARNODE, 1, &data) && data)
		{
			if (GetIdString(machine, (VPVOID)data, idstr))
				id= (ULONG)strings.add((const char*)idstr);
			else
				id= (ULONG)strings.add("unknown");
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lx %s= FUNCGETOWNER()\n",id, (id?(char*)id:"(null)"));
#endif	
		
	return machine.push((VMREGTYPE)id);
}

bool FUNCGETOWNERINFO::execute(void)
{
	VPVOID refr;
	long id = 0;
	long rankVar = 0;
	long type = 0;
	if (GetTargetData(machine, &refr))
	{
		OwnerInfo * info = reinterpret_cast<OwnerInfo*>(GetAttachPointer(machine, refr, VARNODE));
		if (info)
		{
			BaseRecord * rec = reinterpret_cast<BaseRecord*>(info->owner);
			if (rec)
			{
				type = rec->recordType;
				if (type == NPC)
				{
					NPCBaseRecord * npc = reinterpret_cast<NPCBaseRecord*>(rec);
					id = reinterpret_cast<long>(strings.add(npc->IDStringPtr));
					if (info->rankVar.variable)
					{
						GLOBRecord * global = reinterpret_cast<GLOBRecord*>(info->rankVar.variable);
						rankVar = reinterpret_cast<long>(strings.add(global->id));
					}
				}
				else if (type == FACTION)
				{
					FACTRecord * fact = reinterpret_cast<FACTRecord*>(rec);
					id = reinterpret_cast<long>(strings.add(fact->id));
					rankVar = info->rankVar.rank;
				}
			}
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lx %s= FUNCGETOWNER()\n",id, (id?(char*)id:"(null)"));
#endif	
		
	return (machine.push(rankVar) && machine.push(id) && machine.push(type));
}


bool FUNCGETWEIGHT::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	ULONG offset = 0;
	ULONG count = 0;
	VMFLOAT weight = 0.0;
	
	if (GetTargetData(machine, &refr, &temp, &type))
	{
		if ( offset = offsetOfWeight(type) )
			GetOffsetData(machine,temp,offset,(ULONG*)&weight);
		
		if ( GetAttachData(machine, refr, VARNODE, 0, &count) )
			weight *= count;
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETWEIGHT()\n",weight);
#endif	
	return machine.push((VMFLOAT)weight);
}


bool FUNCGETENCUMB::execute(void)
{
	bool result= false;
	VMPTR<TES3REFERENCE*> pref(machine);
	VMPTR<TES3REFERENCE> ref(machine);
	VMPTR<TES3LISTNODE> stacknode(machine);
	VMPTR<TES3STACK> thisstack(machine);
	VMPTR<TES3TEMPLATE> templ(machine);
	VPLISTNODE pstacknode= 0;
	
	bool leveled = false;
	VMFLOAT totalweight = 0.0;
	VMFLOAT weight = 0.0;
	int i;
	try
	{
		pref= (TES3REFERENCE**)reltolinear(SCRIPTTARGETREF_IMAGE);
		if (!*pref) throw 0;
	
		ref= *pref;
		if ( REFERENCE::GetInventory(machine,*pref,pstacknode) )
		{
			while ( pstacknode )
			{
				stacknode = pstacknode;
				if (stacknode->dataptr)
				{
					thisstack = (VPSTACK)stacknode->dataptr;
					if (thisstack->templ)
					{
						templ = thisstack->templ;
						if ( i = offsetOfWeight(templ->type) ) 
						{
							machine.ReadMem(LONGOFFSET(thisstack->templ,i),&weight,sizeof(weight));
							totalweight += (weight * abs((int)thisstack->count));
						}
						else if ( templ->type == 'IVEL' )
						{
							totalweight += 0.000001;
							leveled = true;
						}
					}
				}
				pstacknode = stacknode->next;
			}
		}
		if ( leveled )
			totalweight *= -1;
		result= machine.push((VMFLOAT)totalweight);
	}
	catch(...)
	{
		result= machine.push((VMFLOAT)totalweight);
	}
	
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETENCUMB() %s\n",totalweight,result?"succeeded":"failed");
#endif	
	return result;
}


bool FUNCGETCONDITION::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	VMLONG condition = 0;
	
	if (GetTargetData(machine, &refr, &temp, &type))
	{
        if (type == LOCK || type == REPAIR || type == WEAPON || type == ARMOR || type == PROBE)
		{
			if (!GetAttachData(machine, refr, VARNODE, 3, condition))
			{
				condition = GetMaxCondition(machine, temp, type);
			}
		}
	}
	
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCGETCONDITION()\n",condition);
#endif	
	return machine.push(condition);
}

bool FUNCGETMAXCOND::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	ULONG value = 0;
	
	if (GetTargetData(machine, &refr, &temp, &type))
	{
        value = GetMaxCondition(machine, temp, type);
	}
	
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCGETMAXCOND()\n",value);
#endif	
	return machine.push((VMREGTYPE)value);
}

bool FUNCGETCHARGE::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	ULONG ench = 0;
	ULONG value = 0;
	VMFLOAT charge = 0.0;
	
	if (GetTargetData(machine, &refr, &temp, &type))
	{
		if (!GetAttachData(machine, refr, VARNODE, 4, charge))
		{
            charge = GetMaxCharge(machine, temp, type);
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETCHARGE()\n",charge);
#endif	
	return machine.push(charge);
}

bool FUNCGETMAXCHARGE::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	ULONG ench = 0;
	ULONG value = 0;
	VMFLOAT maxcharge = 0.0;
	
	if (GetTargetData(machine, &refr, &temp, &type))
	{
        maxcharge = GetMaxCharge(machine, temp, type);
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETMAXCHARGE()\n",maxcharge);
#endif	
	return machine.push(maxcharge);
}

bool FUNCGETQUALITY::execute(void)
{
	VPVOID refr, temp;
	ULONG type;
	ULONG offset;
	ULONG count = 0;
	VMFLOAT quality = 0.0;
	
	if (GetTargetData(machine, &refr, &temp, &type))
	{
		offset = offsetOfQuality(type);
		if (offset)
			GetOffsetData(machine,temp,offset,(ULONG*)&quality);
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETQUALITY()\n",quality);
#endif	
	return machine.push((VMFLOAT)quality);
}

bool FUNCGETNAME::execute(void)
{
	VPVOID refr, temp, base;
	ULONG type;
	VPVOID addr = 0;
	ULONG str = 0;
	char namestr[129];
	namestr[128] = 0;
	if (GetTargetData(machine, &refr, &temp, &type, &base))
	{
		if (type == NPC || type == CREA)
			GetOffsetData(machine,base?base:temp,0x1c,(ULONG*)&addr);
		else if (type == CONT) // CONT
			GetOffsetData(machine,temp,0x1b,(ULONG*)&addr);
		else if (type == LIGHT) // LIGH
			GetOffsetData(machine,temp,0x12,(ULONG*)&addr);
		else if (type == CLOTHING || type == ARMOR || type == WEAPON
			|| type == MISC || type == BOOK || type == ALCHEMY || type == AMMO)
			GetOffsetData(machine,temp,0x11,(ULONG*)&addr);
		else if (type == ACTIVATOR)
			GetOffsetData(machine,temp,0xe,(ULONG*)&addr);
		else if (type == DOOR)
			addr = LONGOFFSET(temp,0x0d);
		else if (type == APPARATUS)
			addr = LONGOFFSET(temp,0x19);
		else if (type == INGREDIENT || type == REPAIR || type == PROBE || type == LOCKPICK)
			addr = LONGOFFSET(temp,0x11);

		if (addr && machine.ReadMem((VPVOID)addr, namestr, 128))
				str = (ULONG)strings.add((const char*)namestr);
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lx %s= FUNCGETNAME()\n",str, (str?(char*)str:"(null)"));
#endif	
		
	return machine.push((VMREGTYPE)str);
}

//Tp21 22-08-2006: xSetName
//TODO xSetName, very buggy
bool FUNCSETNAME::execute(void)
{
	VPVOID refr, temp, base;
	ULONG type;
	VPVOID addr = 0;
	ULONG str = 0;

	VMREGTYPE newname = 0;
	const char* string= "null";
	

	if (GetTargetData(machine, &refr, &temp, &type, &base))
	{
		if(machine.pop(newname) && (string=machine.GetString((VPVOID)newname))!=0)
		{
			//if the type is npc_ or crea
		if (type == NPC || type == CREA)
			GetOffsetData(machine,base?base:temp,0x1c,(ULONG*)&addr);
		else if (type == CONT)
			GetOffsetData(machine,temp,0x1b,(ULONG*)&addr);
		else if (type == LIGHT)
			GetOffsetData(machine,temp,0x12,(ULONG*)&addr);
		else if (type == CLOTHING || type == ARMOR || type == WEAPON 
			|| type == MISC || type == BOOK || type == ALCHEMY || type == AMMO)
			GetOffsetData(machine,temp,0x11,(ULONG*)&addr);
		else if (type == ACTIVATOR)
			GetOffsetData(machine,temp,0xe,(ULONG*)&addr);
		else if (type == DOOR)
			addr = LONGOFFSET(temp,0x0d);
		else if (type == APPARATUS)
			addr = LONGOFFSET(temp,0x19);
		else if (type == INGREDIENT || type == REPAIR || type == PROBE || type == LOCKPICK)
			addr = LONGOFFSET(temp,0x11);

			//this writes the string (max char= 128!)
			//no check for maximum characters
			if ( addr )
				machine.WriteMem((VPVOID)addr, string, 128);

//			if addr is true, read the name from memory
//			if (addr && machine.ReadMem((VPVOID)addr, namestr, 128))
//					str = (ULONG)strings.add((const char*)namestr);
//					machine.WriteMem((VPVOID)addr, namestr, 128);
		}
	}
#ifdef DEBUGGING
//	cLog::mLogMessage("%lx %s %s= FUNCSETNAME()\n",str, (str?(char*)str:"(null)"), string);
#endif	

	return true;
}

bool FUNCGETBASEID::execute(void)
{
	VPVOID refr, temp, base;
	ULONG type;
	ULONG id = 0;
	char idstr[129];
	idstr[128] = 0;
	if (GetTargetData(machine, &refr, &temp, &type, &base))
	{
		if (base && GetIdString(machine, (VPVOID)base, idstr))
			id= (ULONG)strings.add((const char*)idstr);
		else if (temp && GetIdString(machine, (VPVOID)temp, idstr))
			id= (ULONG)strings.add((const char*)idstr);
		else
			id= (ULONG)strings.add("unknown");
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lx %s= FUNCGETBASEID()\n",id, (id?(char*)id:"(null)"));
#endif	
		
	return machine.push((VMREGTYPE)id);
}



bool FUNCGETBASESTR::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x96, (ULONG*)&value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASESTR()\n",value);
#endif	
	return machine.push((VMFLOAT)value);
}

bool FUNCGETBASEINT::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x99, (ULONG*)&value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEINT()\n",value);
#endif	
	return machine.push((VMFLOAT)value);
}

bool FUNCGETBASEWIL::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x9c, (ULONG*)&value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEWIL()\n",value);
#endif	
	return machine.push((VMFLOAT)value);
}

bool FUNCGETBASEAGI::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0x9f, (ULONG*)&value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEAGI()\n",value);
#endif	
	return machine.push((VMFLOAT)value);
}

bool FUNCGETBASESPE::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0xa2, (ULONG*)&value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASESPE()\n",value);
#endif	
	return machine.push((VMFLOAT)value);
}

bool FUNCGETBASEEND::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0xa5, (ULONG*)&value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEEND()\n",value);
#endif	
	return machine.push((VMFLOAT)value);
}

bool FUNCGETBASEPER::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0xa8, (ULONG*)&value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASEPER()\n",value);
#endif	
	return machine.push((VMFLOAT)value);
}

bool FUNCGETBASELUC::execute(void)
{
	VPVOID refr;
	VMFLOAT value = -1.0;
	
	if (GetTargetData(machine, &refr))
		GetAttachData(machine, refr, 8, 0xab, (ULONG*)&value);
#ifdef DEBUGGING
	cLog::mLogMessage("%f= FUNCGETBASELUC()\n",value);
#endif	
	return machine.push((VMFLOAT)value);
}



bool FUNCISFEMALE::execute(void)
{
	VPVOID refr, temp;
	ULONG value = 0;
	
	if (GetTargetData(machine, &refr, &temp))
		if (GetOffsetData(machine, temp, OFFSET_NPCFLAGS, &value))
			value %= 2;	// lowest bit
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCISFEMALE()\n",value);
#endif	
	return machine.push((VMREGTYPE)value);
}

bool FUNCMYCELLID::execute(void)
{
	VPVOID refr, addr;
	ULONG data;
	ULONG id = 0;
	char idstr[129];
	idstr[128] = 0;
	if (GetTargetData(machine, &refr))
	{
		if (GetOffsetData(machine, refr, 5, &data) && data)
			if (GetOffsetData(machine, (VPVOID)data, 3, (ULONG*)&addr) && addr)
				if (GetOffsetData(machine, addr, 4, &data) && data)
					if (machine.ReadMem((VPVOID)data, (VPVOID)idstr, 128))
						id= (ULONG)strings.add((const char*)idstr);
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lx %s= FUNCMYCELLID()\n",id, (id?(char*)id:"(null)"));
#endif	
		
	return machine.push((VMREGTYPE)id);
}




bool FUNCGETBASEGOLD::execute(void)
{
	VPVOID refr, temp, base;
	ULONG type;
	ULONG value = 0;
	ULONG gold = 0;
	
	if (GetTargetData(machine, &refr, &temp, &type, &base))
	{
		if (!base)
			base = temp;
		if ( type == NPC )
			GetOffsetData(machine, base, OFFSET_NPCBASEGOLD, &gold);
		else if ( type == CREA )
			GetOffsetData(machine, base, OFFSET_CREBASEGOLD, &gold);
		gold %= 65536; // it's a short here, so lower half only
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCGETBASEGOLD()\n",gold);
#endif	
	return machine.push((VMREGTYPE)gold);
}

bool FUNCGETGOLD::execute(void)
{
	VPVOID refr, temp, base;
	ULONG type;
	ULONG value = 0;
	ULONG gold = 0;
	
	if (GetTargetData(machine, &refr, &temp, &type, &base))
	{
		if (!GetAttachData(machine, refr, 8, OFFSET_GOLD, &gold))
		{
			if (!base)
				base = temp;
			if ( type == NPC )
				GetOffsetData(machine, base, OFFSET_NPCBASEGOLD, &gold);
			else if ( type == CREA )
				GetOffsetData(machine, base, OFFSET_CREBASEGOLD, &gold);
			gold %= 65536; // it's a short here, so lower half only
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCGETGOLD()\n",gold);
#endif	
	return machine.push((VMREGTYPE)gold);
}

bool FUNCSETBASEGOLD::execute(void)
{
	VPVOID refr, temp, base;
	ULONG type;
	ULONG value = 0;
	VMREGTYPE gold = 0;
	
	if (GetTargetData(machine, &refr, &temp, &type, &base))
	{
        	if (machine.pop(gold))
		{
			if (!base)
				base = temp;
			if ( type == NPC )
				if (GetOffsetData(machine, base, OFFSET_NPCBASEGOLD, &value))
				{
					value /= 65536;
					gold %= 65536;  // gold is hold the lower half here
					gold = gold + value * 65536;
					SetOffsetData(machine, base, OFFSET_NPCBASEGOLD, gold);
				}
			else if ( type == CREA )
				if (GetOffsetData(machine, base, OFFSET_CREBASEGOLD, &value))
				{
					value /= 65536;
					gold %= 65536;  // gold is hold the lower half here
					gold = gold + value * 65536;
					SetOffsetData(machine, base, OFFSET_CREBASEGOLD, gold);
				}
		}
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCSETBASEGOLD()\n",gold);
#endif	
	return machine.push((VMREGTYPE)gold);
}

bool FUNCSETGOLD::execute(void)
{
	bool result = false;
	VPVOID refr;
	ULONG value = 0;
	VMREGTYPE gold = 0;
	
	if (GetTargetData(machine, &refr))
	{
        	if (machine.pop(gold))
			result = SetAttachData(machine, refr, 8, OFFSET_GOLD, (ULONG)gold);
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCSETGOLD()\n",gold);
#endif	
	return result;
}



bool FUNCISTRADER::execute(void)
{
	VPVOID refr, temp, base;
	ULONG type;
	ULONG flags = 0;
	ULONG cflags = 0;
	ULONG value = 0;
	
	if (GetTargetData(machine, &refr, &temp, &type, &base) && type == NPC)
	{
		if (!base || !GetOffsetData(machine, base, 1, &value) || value != NPC)
			base = temp;  // in case npc isn't currently loaded
		
		GetOffsetData(machine,base,OFFSET_NPCSERVICES,&flags);
		flags &= 0x000037FF;
	
		if (GetOffsetData(machine,base,OFFSET_NPCCLASS,(ULONG*)&temp) && temp) // class info
		{
			if (GetOffsetData(machine,temp,1,&value) && value == 'SALC') 
			{
				GetOffsetData(machine,temp,OFFSET_CLASSSERVICES,&cflags);
				cflags &= 0x000037FF;
			}
		}
		
		flags |= cflags; 
	}
	
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCISTRADER()\n",flags);
#endif	
	return machine.push((VMREGTYPE)flags);
}

bool FUNCISTRAINER::execute(void)
{
	VPVOID refr, temp, base;
	ULONG type;
	ULONG flags = 0;
	ULONG cflags = 0;
	ULONG value = 0;
	
	if (GetTargetData(machine, &refr, &temp, &type, &base) && type == NPC)
	{
		if (!base || !GetOffsetData(machine, base, 1, &value) || value != NPC)
			base = temp;  // in case npc isn't currently loaded
		
		GetOffsetData(machine,base,OFFSET_NPCSERVICES,&flags);
		flags &= 0x00004000;
	
		if (GetOffsetData(machine,base,OFFSET_NPCCLASS,(ULONG*)&temp) && temp) // class info
		{
			if (GetOffsetData(machine,temp,1,&value) && value == 'SALC') 
			{
				GetOffsetData(machine,temp,OFFSET_CLASSSERVICES,&cflags);
				cflags &= 0x00004000;
			}
		}
		if ( flags | cflags ) 
			flags = 1;
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCISTRAINER()\n",flags);
#endif	
	return machine.push((VMREGTYPE)flags);
}

bool FUNCISPROVIDER::execute(void)
{
	VPVOID refr, temp, base;
	ULONG type;
	ULONG flags = 0;
	ULONG cflags = 0;
	ULONG value = 0;
	
	if (GetTargetData(machine, &refr, &temp, &type, &base) && type == NPC)
	{
		if (!base || !GetOffsetData(machine, base, 1, &value) || value != NPC)
			base = temp;  // in case npc isn't currently loaded
		
		GetOffsetData(machine,base,OFFSET_NPCSERVICES,&flags);
		flags &= 0x00038800;
	
		if (GetOffsetData(machine,base,OFFSET_NPCCLASS,(ULONG*)&temp) && temp) // class info
		{
			if (GetOffsetData(machine,temp,1,&value) && value == 'SALC') 
			{
				GetOffsetData(machine,temp,OFFSET_CLASSSERVICES,&cflags);
				cflags &= 0x00038800;
			}
		}
		flags |= cflags;
	}
#ifdef DEBUGGING
	cLog::mLogMessage("%lu= FUNCISPROVIDER()\n",flags);
#endif	
	return machine.push((VMREGTYPE)flags);
}



// 2005-07-12  CDC  More powerful alternatives to the IsTrader/Trainer/Provider functions
bool FUNCGETSERVICE::execute(void)
{
	VPVOID refr, temp, base;
	ULONG type;
	ULONG flags = 0;
	ULONG cflags = 0;
	ULONG mask = 0x0003FFFF;
	
	if (machine.pop((VMREGTYPE&)mask)) 
		mask &= 0x0003FFFF;
	if (GetTargetData(machine, &refr, &temp, &type, &base) && type == NPC)
	{
		if (!base || !GetOffsetData(machine, base, 1, &type) || type != NPC)
			base = temp;  // in case npc isn't currently loaded
		
		GetOffsetData(machine,base,OFFSET_NPCSERVICES,&flags);		// the npc data
		//if (GetOffsetData(machine,base,OFFSET_NPCCLASS,(ULONG*)&temp) 	// class info if available?
		//	&& temp && GetOffsetData(machine,temp,1,&type) && type == 'SALC')
		//	GetOffsetData(machine,temp,OFFSET_CLASSSERVICES,&cflags);
		flags |= cflags; 
		flags &= mask;
	}
	return machine.push((VMREGTYPE)flags);
}

bool FUNCSETSERVICE::execute(void)
{
	VPVOID refr, temp, base;
	VMREGTYPE data;
	ULONG type;
	ULONG flags = 0;
	ULONG cflags = 0;
	
	if (!machine.pop(data)) return false;

	flags = data & 0x0003FFFF;
	if (GetTargetData(machine, &refr, &temp, &type, &base) && type == NPC)
	{
		if (!base || !GetOffsetData(machine, base, 1, &type) || type != NPC)
			base = temp;  // in case npc isn't currently loaded
		
		return SetOffsetData(machine,base,OFFSET_NPCSERVICES,flags);
	}
	return false;
}

bool FUNCMODSERVICE::execute(void)
{
	VPVOID refr, temp, base;
	VMREGTYPE data;
	ULONG type;
	ULONG flags = 0;
	ULONG cflags = 0;
	
	if (!machine.pop(data)) return false;
	if (GetTargetData(machine, &refr, &temp, &type, &base) && type == NPC)
	{
		if (!base || !GetOffsetData(machine, base, 1, &type) || type != NPC)
			base = temp;  // in case npc isn't currently loaded
		
		GetOffsetData(machine,base,OFFSET_NPCSERVICES,&flags);		// the npc data
		if (GetOffsetData(machine,base,OFFSET_NPCCLASS,(ULONG*)&temp) 	// class info if available?
			&& temp && GetOffsetData(machine,temp,1,&type) && type == CLASS)
			GetOffsetData(machine,temp,OFFSET_CLASSSERVICES,&cflags);
		flags |= cflags; 

	if ( ((signed long)data) < 0 )	// Want to remove services
		flags = flags & (~((ULONG)(0-(signed long)data)));
	else				// Want to add services
		flags = flags | (data & 0x0003FFFF);
		return SetOffsetData(machine,base,OFFSET_NPCSERVICES,flags);
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////


// Return maximum condition of item.
static ULONG GetMaxCondition(TES3MACHINE &machine, VPVOID refr, ULONG type)
{
    ULONG value = 0;
    ULONG offset = offsetOfCondition(type); // misnomer, actually MaxCondition
    if ( offset ) {
        GetOffsetData(machine, refr, offset, &value);
        if ( type == WEAPON) {
            // Weapon uses upper 16 bits of value
            value >>= 16;
        }
    }
    return value;
}

// Return maximum charge of item.
static ULONG GetMaxCharge(TES3MACHINE &machine, VPVOID refr, ULONG type)
{
	VPVOID offset = 0;
	ULONG value = 0;
    offset = GetMaxChargeOffset(machine, refr, type);
    if (offset) {  // found enchantment record
        // actually stored as integer
		GetOffsetData(machine,offset,0xc,&value);
	}
    return value;
}

// Return offset of maximum charge for item, if possible. Return 0 if
// not found.
static VPVOID GetMaxChargeOffset(TES3MACHINE &machine, VPVOID refr, ULONG type)
{
    ULONG ench = 0;
    ULONG autoCalc = 0;

    if ( type == ARMOR ) {
        // Armour.
		GetOffsetData(machine, refr, 0x30, &ench);
    } else if ( type == CLOTHING ) {
        // Clothing.
		GetOffsetData(machine, refr, 0x2d, &ench);
    } else if ( type == WEAPON ) {
        // Weapon.
		GetOffsetData(machine, refr, 0x1d, &ench);
    }

    if (ench) {  // found enchantment record
		GetOffsetData(machine,reinterpret_cast<VPVOID>(ench),0x10,&autoCalc);
    }
    // Only the low-order byte of 'autoCalc' is used.
    // FIXME: it is not clear if this is correct.
    return ((autoCalc & 0xFF)!=0) ? reinterpret_cast<VPVOID>(ench) : NULL;
}

static SPELRecord * GetSpellRecord(VMLONG const spellId, TES3MACHINE & machine)
{
	SPELRecord * spell = 0;
	if (spellId != 0)
	{
		char const * idString = machine.GetString(reinterpret_cast<VPVOID>(spellId));
		TES3CELLMASTER* cellMaster = *(reinterpret_cast<TES3CELLMASTER**>reltolinear(MASTERCELL_IMAGE));
		spell = reinterpret_cast<SPELRecord*>(cellMaster->recordLists->spellsList->head);
		// this list should only contain spells, but check the type anyway to be safe
		while (spell != 0 && !(spell->recordType == SPELL && strcmp(idString, spell->id) == 0))
		{
			spell = spell->nextRecord;
		}
	}
	return spell;
}

static ENCHRecord * GetEnchantmentRecord(VMLONG const enchId, TES3MACHINE & machine)
{
	ENCHRecord * ench = 0;
	if (enchId != 0)
	{
		char const * idString = machine.GetString(reinterpret_cast<VPVOID>(enchId));
		TES3CELLMASTER* cellMaster = *(reinterpret_cast<TES3CELLMASTER**>reltolinear(MASTERCELL_IMAGE));
		ench = reinterpret_cast<ENCHRecord*>(cellMaster->recordLists->enchantmentsList->head);
		// this list contains records that are not enchantments, so check the type
		while (ench != 0 && !(ench->recordType == ENCHANTMENT && strcmp(idString, ench->id) == 0))
		{
			ench = ench->nextRecord;
		}
	}
	return ench;
}

static VMSHORT CountEffects(Effect const * effects)
{
	VMSHORT numEffects = 0;
	if (effects)
	{
		//count the number of effect slots in use
		for (int i = 0; i < 8; ++i)
		{
			if (effects[i].effectId != kNoEffect)
			{
				++numEffects;
			}
			else
			{
				break;
			}
		}
	}
	return numEffects;
}

static float GetSkillRequirement(TES3MACHINE& machine, long skill_id)
{
	MACPRecord const* const macp = machine.GetMacpRecord();
	TES3CELLMASTER const* const cell_master =
		*(reinterpret_cast<TES3CELLMASTER**>reltolinear(MASTERCELL_IMAGE));
	GMSTRecord const* const* gmsts = cell_master->recordLists->GMSTs;
	MACPRecord::Skill const& skill = macp->skills[skill_id];
	float requirement = 1.0 + skill.base;
	if (skill.skillType == Misc) {
		requirement *= gmsts[fMiscSkillBonus]->value.float_value;
	}
	else if (skill.skillType == Minor) {
		requirement *= gmsts[fMinorSkillBonus]->value.float_value;
	}
	else if (skill.skillType == Major) {
		requirement *= gmsts[fMajorSkillBonus]->value.float_value;
	}
	NPCCopyRecord const* const npc_copy =
		reinterpret_cast<NPCCopyRecord*>(macp->reference->templ);
	CLASRecord const* const klass = npc_copy->baseNPC->characterClass;
	SKILRecord const& skill_record =
		cell_master->recordLists->skills[skill_id];
	if (klass->specialization == skill_record.specialization) {
		requirement *= gmsts[fSpecialSkillBonus]->value.float_value;
	}
	return requirement;
}

static void CheckForLevelUp(long const progress)
{
	TES3CELLMASTER* cellMaster = *(reinterpret_cast<TES3CELLMASTER**>reltolinear(MASTERCELL_IMAGE));
	GMSTRecord ** gmsts = cellMaster->recordLists->GMSTs;
	if (progress >= gmsts[iLevelupTotal]->value.long_value)
	{
		int const loadMessage = 0x40F930;
		int const displayMessage = 0x5F90C0;
		__asm
		{
			mov ecx, dword ptr [0x7C67DC];
			push 0x1;
			push 0x0;
			push 0x2AA;
			call loadMessage;
			push eax;
			call displayMessage;
		}
	}
}

static Effect * GetEffects(long const type, long const id, TES3MACHINE & machine)
{
	Effect * effects = 0;

	if (type == SPELL)
	{
		SPELRecord * spell = GetSpellRecord(id, machine);
		if (spell)
		{
			effects = spell->effects;
		}
	}
	else if (type == ENCHANTMENT)
	{
		ENCHRecord * ench = GetEnchantmentRecord(id, machine);
		if (ench)
		{
			effects = ench->effects;
		}
	}
	return effects;
}

static VMLONG SetEffect(Effect * effects, VMLONG index, VMLONG effect_id, 
						VMLONG skill_attribute_id, VMLONG range, VMLONG area, 
						VMLONG duration, VMLONG minimum_magnitude,
						VMLONG maximum_magnitude)
{
	VMLONG result = 0;
	if (effects && 
		1 <= index && index <= 8 &&
		kFirstMagicEffect <= effect_id && effect_id <= kLastMagicEffect &&
		kFirstRangeType <= range && range <= kLastRangeType) {
		int effect_flags = kMagicEffectFlags[effect_id];
		if ((effect_flags & kCastSelf && range == kSelf) ||
			(effect_flags & kCastTouch && range == kTouch) ||
			(effect_flags & kCastTarget && range == kTarget)) {
			--index; // convert to 0-based array index
			Effect& effect = effects[index];
			effect.effectId = effect_id;
			if (effect_flags & kTargetSkill) {
				effect.skillId = skill_attribute_id;
			}
			else {
				effect.skillId = kNoSkill;
			}
			if (effect_flags & kTargetAttribute) {
				effect.AttributeId = skill_attribute_id;
			}
			else {
				effect.AttributeId = kNoAttribute;
			}
			if (effect_flags & kNoDuration) {
				effect.Duration = 0;
			}
			else {
				effect.Duration = duration;
			}
			if (effect_flags & kNoMagnitude) {
				effect.MagMin = 0;
				effect.MagMax = 0;
			}
			else {
				effect.MagMin = minimum_magnitude;
				effect.MagMax = maximum_magnitude;
			}
			effect.RangeType = range;
			effect.Area = area;
			result = 1;
		}
	}
	return result;
}
