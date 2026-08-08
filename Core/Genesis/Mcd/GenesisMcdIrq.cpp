#include "pch.h"
#include "Genesis/Mcd/GenesisMcdIrq.h"
#include "Utilities/Serializer.h"

//ares MCD::IRQ::raise: if already pending, no-op; otherwise latch pending=enable.
//(A disabled source never latches, matching hardware where the IRQ pin is masked.)
bool GenesisMcdIrq::Raise(Source& s)
{
	if(s.pending) return false;
	s.pending = s.enable;
	Synchronize();
	return true;
}

//ares MCD::IRQ::lower: clear pending if set.
bool GenesisMcdIrq::Lower(Source& s)
{
	if(!s.pending) return false;
	s.pending = false;
	Synchronize();
	return true;
}

//ares MCD::IRQ::synchronize: aggregate every source into the top-level pending flag.
void GenesisMcdIrq::Synchronize()
{
	pending = reset.pending
		|| gpu.pending
		|| external.pending
		|| timer.pending
		|| cdd.pending
		|| cdc.pending
		|| subcode.pending;
}

void GenesisMcdIrq::Serialize(Serializer& s)
{
	auto streamSource = [&s](Source& src) {
		SV(src.enable);
		SV(src.pending);
	};
	streamSource(reset);
	streamSource(gpu);
	streamSource(external);
	streamSource(timer);
	streamSource(cdd);
	streamSource(cdc);
	streamSource(subcode);
	SV(pending);
}
