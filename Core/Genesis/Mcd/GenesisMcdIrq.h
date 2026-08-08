#pragma once
#include "pch.h"
#include "Utilities/ISerializable.h"

//Forward declaration (full definition pulled in by .cpp via Utilities/Serializer.h).
class Serializer;

//Mega CD interrupt controller, ported from ares/md/mcd/irq.cpp.
//
//The MCD sub-CPU has 7 interrupt sources at fixed priority levels, plus a
//special "reset" source that reloads the sub-CPU reset vector:
//   level 1: GPU          (Phase E)
//   level 2: External     (main CPU via 0xA12000 bit 8)
//   level 3: Timer        (Phase C)
//   level 4: CDD          (Phase B)
//   level 5: CDC          (Phase B)
//   level 6: Subcode      (Phase B)
//   reset : reload vector (raised by resetCpu)
//
//Each source has an enable bit and a pending bit. raise() sets pending=enable
//(so a disabled source never actually latches); lower() clears pending. The
//aggregate `pending` flag is recomputed by Synchronize() after every raise/lower.
//The sub-CPU's CheckInterrupts callback (GenesisMcd::CheckSubCpuInterrupts)
//implements the priority dispatch from ares MCD::main().

class GenesisMcdIrq final : public ISerializable
{
public:
	struct Source {
		bool enable = false;
		bool pending = false;
	};

	Source reset;     //special: sub-CPU reset (vector reload)
	Source gpu;       //level 1
	Source external;  //level 2
	Source timer;     //level 3
	Source cdd;       //level 4
	Source cdc;       //level 5
	Source subcode;   //level 6

	bool pending = false;  //aggregate (any source pending)

	//ares MCD::IRQ::raise/lower. Returns true if the source transitioned.
	bool Raise(Source& s);
	bool Lower(Source& s);

	//Recompute the aggregate pending flag from all sources (ares IRQ::synchronize).
	void Synchronize();

	//ISerializable
	void Serialize(Serializer& s) override;
};
