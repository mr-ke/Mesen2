#pragma once
#include "pch.h"
#include "Utilities/ISerializable.h"

class GenesisMcd;
class Serializer;

//Mega CD programmable timer, ported from ares/md/mcd/timer.cpp.
//
//The timer is a simple down-counter clocked every 384 sub-CPU cycles (from the
//MCD::step() divider). When it underflows, it reloads from `frequency` and
//raises the level-3 interrupt. If `frequency` is 0 the timer is disabled.
//
//Register map (sub-CPU internal IO):
//  0xFF8030 (write): timer.counter = timer.frequency = data.byte(0)
//  0xFF8030 (read) : returns timer.frequency
//The level-3 IRQ enable bit lives in the shared 0xFF8032 register
//(GenesisMcdIrq::timer.enable), not in the timer itself — matching ares where
//timer.irq.enable is set by writeIO(0xff8032).
class GenesisMcdTimer final : public ISerializable
{
public:
	void SetMcd(GenesisMcd* mcd) { _mcd = mcd; }

	//ares MCD::Timer::power
	void Power(bool reset);

	//ares MCD::Timer::clock. Called every 384 sub-CPU cycles from Step().
	//  if(frequency && !counter--) { counter = frequency; irq.raise(); }
	void Clock();

	uint8_t GetFrequency() const { return _frequency; }
	void SetFrequency(uint8_t f) { _frequency = f; _counter = f; }

	//ISerializable
	void Serialize(Serializer& s) override;

private:
	GenesisMcd* _mcd = nullptr;
	uint8_t _frequency = 0;  //reload value (also the readable register)
	uint8_t _counter = 0;    //down-counter
};
