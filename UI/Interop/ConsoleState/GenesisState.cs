using System;
using System.Runtime.InteropServices;

namespace Mesen.Interop;

public struct GenesisM68KState : BaseState
{
	[MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
	public UInt32[] D;
	[MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
	public UInt32[] A;
	public UInt32 PC;
	public UInt16 SR;
	public UInt32 SSP;
	[MarshalAs(UnmanagedType.I1)] public bool Stopped;
}

public struct GenesisZ80State : BaseState
{
	public byte A;
	public byte Flags;
	public byte B;
	public byte C;
	public byte D;
	public byte E;
	public byte H;
	public byte L;
	public UInt16 IX;
	public UInt16 IY;
	public UInt16 SP;
	public UInt16 PC;
	public byte I;
	public byte R;
	[MarshalAs(UnmanagedType.I1)] public bool Halted;
}

public struct GenesisVdpState : BaseState
{
	public UInt16 VCounter;
	public UInt16 HCounter;
	[MarshalAs(UnmanagedType.I1)] public bool VBlank;
	[MarshalAs(UnmanagedType.I1)] public bool HBlank;
	[MarshalAs(UnmanagedType.I1)] public bool DisplayEnable;
	[MarshalAs(UnmanagedType.ByValArray, SizeConst = 24)]
	public byte[] Regs;
	[MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)]
	public UInt16[] Cram;
}

public struct GenesisState : BaseState
{
	public GenesisM68KState M68K;
	public GenesisZ80State Z80;
	public GenesisVdpState Vdp;
	public UInt32 FrameCount;
	public int Region;
}
