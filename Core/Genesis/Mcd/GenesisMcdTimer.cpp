#include "pch.h"
#include "Genesis/Mcd/GenesisMcdTimer.h"
#include "Genesis/Mcd/GenesisMcd.h"
#include "Genesis/Mcd/GenesisMcdIrq.h"
#include "Utilities/Serializer.h"

//ares MCD::Timer::power
void GenesisMcdTimer::Power(bool reset)
{
	(void)reset;
	_frequency = 0;
	_counter = 0;
	//timer.irq is reset by GenesisMcd::Power (it owns the IRQ source).
}

//ares MCD::Timer::clock:
//  if(frequency && !counter--) { counter = frequency; irq.raise(); }
//
//`!counter--` tests the value BEFORE decrement: when counter is 0 the test is
//true, counter wraps to 255, then we reload from frequency and raise the IRQ.
//So the period is (frequency + 1) timer ticks; each tick is 384 sub-CPU cycles.
void GenesisMcdTimer::Clock()
{
	if(!_frequency) return;
	if(_counter != 0) {
		_counter--;
		return;
	}
	//underflow: reload + raise level-3 IRQ.
	_counter = _frequency;
	if(_mcd) _mcd->RaiseTimerIrq();
}

void GenesisMcdTimer::Serialize(Serializer& s)
{
	SV(_frequency);
	SV(_counter);
}
