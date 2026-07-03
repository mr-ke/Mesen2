#pragma once
#include "pch.h"
#include "Shared/SettingTypes.h"
#include "Utilities/ISerializable.h"

class Emulator;
class SoundMixer;
class EmuSettings;
class GenesisConsole;
struct blip_t;

// Yamaha YM2612 (OPN2) - FM synthesis chip for the Sega Mega Drive / Genesis.
//
// This is a faithful native port of ares's YM2612 (ares/component/audio/ym2612/,
// author Talarubi), converted to standard C++ types and Mesen2's serialization
// (SV()) / audio (blip_buf + SoundMixer) infrastructure. The algorithm itself
// (envelope, phase, SSG-EG, algorithm routing, DAC) is preserved verbatim.
//
// Clocking: the YM2612 internal clock runs at master/7 (M68K bus clock).
// Each YM2612::clock() call produces one stereo sample and represents 144
// internal cycles = 1008 master clocks. Sample rate ~ master/1008 (~53kHz NTSC).
class GenesisYm2612 final : public ISerializable
{
public:
	GenesisYm2612(Emulator* emu, GenesisConsole* console);
	~GenesisYm2612();

	void SetRegion(ConsoleRegion region);

	//Advance emulation by master-clock delta, producing samples as needed.
	void Run();
	//Flush queued samples to SoundMixer.
	void PlayQueuedAudio();

	//Register interface (ports 0/1 = address, 0/1 = data, as on the M68K bus).
	uint8_t ReadStatus();
	void WriteAddress(uint8_t port, uint8_t data);
	void WriteData(uint8_t port, uint8_t data);

	void Serialize(Serializer& s) override;

	//Public so GenesisYm2612.cpp can use the enum name.
	enum EnvState : uint32_t { Attack, Decay, Sustain, Release };

private:
	// --- Core chip state (ported from ares ym2612.hpp) ---
	//Forward-declared at class scope so Operator methods can reference it.
	struct Channel;

	struct IO {
		uint16_t address = 0;   //n9
	} _io;

	struct LFO {
		uint8_t  enable = 0;    //n1
		uint8_t  rate = 0;      //n3
		uint32_t clock = 0;     //n32
		uint32_t divider = 0;   //n32
	} _lfo;

	struct DAC {
		uint8_t enable = 0;     //n1
		uint8_t sample = 0x80;  //n8
	} _dac;

	struct Envelope {
		uint16_t clock = 0;     //n12
		uint32_t divider = 0;   //n32
	} _envelope;

	struct TimerA {
		uint8_t  enable = 0;      //n1
		uint8_t  enableLatch = 0; //n1
		uint8_t  irq = 0;         //n1
		uint8_t  line = 0;        //n1
		uint16_t period = 0;      //n10
		uint16_t counter = 0;     //n10
		void Run();
	} _timerA;

	struct TimerB {
		uint8_t  enable = 0;      //n1
		uint8_t  enableLatch = 0; //n1
		uint8_t  irq = 0;         //n1
		uint8_t  line = 0;        //n1
		uint8_t  period = 0;      //n8
		uint8_t  counter = 0;     //n8
		uint8_t  divider = 0;     //n4
		void Run();
	} _timerB;

	struct Operator {
		uint8_t  keyOn = 0;          //n1
		uint8_t  keyLine = 0;        //n1
		uint8_t  tremoloEnable = 0;  //n1
		uint8_t  keyScale = 0;       //n5
		uint8_t  detune = 0;         //n3
		uint8_t  multiple = 0;       //n4
		uint8_t  totalLevel = 0;     //n7

		uint16_t outputLevel = 0x1fff; //n16 (signed in ares, kept unsigned for SV)
		int16_t  output = 0;           //i16
		int16_t  prior = 0;            //i16
		int16_t  priorBuffer = 0;      //i16

		struct Pitch {
			uint16_t value = 0;   //n11
			uint16_t reload = 0;  //n11
			uint16_t latch = 0;   //n11
		} pitch;

		struct Octave {
			uint8_t value = 0;   //n3
			uint8_t reload = 0;  //n3
			uint8_t latch = 0;   //n3
		} octave;

		struct Phase {
			uint32_t value = 0;  //n20
			uint32_t delta = 0;  //n20
		} phase;

		struct Env {
			uint32_t state = Release;       //enum
			int32_t  rate = 0;              //s32
			int32_t  divider = 11;          //s32
			uint32_t steps = 0;             //n32
			uint16_t value = 0x3ff;         //n10

			uint8_t  rateScaling = 0;       //n2
			uint8_t  attackRate = 0;        //n5
			uint8_t  decayRate = 0;         //n5
			uint8_t  sustainRate = 0;       //n5
			uint8_t  sustainLevel = 0;      //n4
			uint8_t  releaseRate = 1;       //n5
		} envelope;

		struct SSG {
			uint8_t enable = 0;      //n1
			uint8_t attack = 0;      //n1
			uint8_t alternate = 0;   //n1
			uint8_t hold = 0;        //n1
			uint8_t invert = 0;      //n1
		} ssg;

		//Methods (definitions in GenesisYm2612.cpp; take a reference to the
		//owning GenesisYm2612 + channel since ares used back-references).
		void UpdateKeyState(GenesisYm2612& ym, Channel& channel);
		void RunEnvelope(GenesisYm2612& ym, Channel& channel);
		void RunPhase(GenesisYm2612& ym, Channel& channel);
		void UpdateEnvelope(GenesisYm2612& ym);
		void UpdatePitch(GenesisYm2612& ym, Channel& channel);
		void UpdatePhase(GenesisYm2612& ym, Channel& channel);
		void UpdateLevel(GenesisYm2612& ym, Channel& channel);
	};

	struct Channel : public ISerializable {
		uint8_t leftEnable = 1;    //n1
		uint8_t rightEnable = 1;   //n1
		uint8_t algorithm = 0;     //n3
		uint8_t feedback = 0;      //n3
		uint8_t vibrato = 0;       //n3
		uint8_t tremolo = 0;       //n2
		uint8_t mode = 0;          //n2

		Operator operators[4];

		void Power(GenesisYm2612& ym);
			void Serialize(Serializer& s) override;
	};

	Channel _channels[6];

	//Lookup tables (built in Power()).
	uint16_t _sine[0x400];
	int16_t  _pow2[0x200];

	// --- Mesen2 integration state ---
	Emulator*         _emu = nullptr;
	SoundMixer*       _soundMixer = nullptr;
	EmuSettings*      _settings = nullptr;
	GenesisConsole*   _console = nullptr;

	static constexpr int SampleRate = 96000;
	static constexpr int MaxSamples = 4000;

	int16_t* _soundBuffer = nullptr;
	blip_t*  _leftChannel = nullptr;
	blip_t*  _rightChannel = nullptr;

	uint64_t _masterClock = 0;      //master clocks consumed so far
	uint64_t _clockCounter = 0;     //samples produced this frame (blip time)
	int16_t  _prevLeft = 0;
	int16_t  _prevRight = 0;

	//Every 1008 master clocks = 1 YM2612 sample.
	static constexpr uint32_t MasterClocksPerSample = 1008;

	//Internal: produce one stereo sample (ares YM2612::clock).
	void ClockOnce();
	void Power();

	//Static lookup tables (ares constants.cpp).
	static const uint8_t _lfoDividers[8];
	static const uint8_t _vibratos[8][16];
	static const uint8_t _tremolos[4];
	static const uint8_t _detunes[3][8];
	struct EnvelopeRate { uint32_t divider; uint32_t steps[4]; };
	static const EnvelopeRate _envelopeRates[16];
};
