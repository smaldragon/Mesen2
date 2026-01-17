#pragma once
#include "pch.h"
#include "NES/BaseMapper.h"
#include "NES/Mappers/Homebrew/FlashSST39SF040.h"
#include "Shared/BatteryManager.h"
#include "Utilities/Patches/IpsPatcher.h"
#include "Utilities/Serializer.h"

#include "NES/NesConsole.h"
#include "NES/NesCpu.h"
#include "NES/BaseNesPpu.h"

class BnuyRom : public BaseMapper
{
private:
	unique_ptr<FlashSST39SF040> _flash;
	vector<uint8_t> _orgPrgRom;

	uint8_t   _prgBank = 0;
	uint8_t   _chrBanks[4] = {};
	uint8_t   _irqPrescaler = 0;
	uint8_t   _irqCounter = 0;
	int       _banksPerWindow = 0;
protected:
	uint16_t GetPrgPageSize() override { return 0x8000; }
	uint16_t GetChrPageSize() override { return 0x0800; }
	uint16_t RegisterStartAddress() override { return 0x8000; }
	uint16_t RegisterEndAddress() override { return 0xFFFF; }
	uint32_t GetWorkRamPageSize() override { return 0x2000; }
	bool EnableCustomVramRead() override { return true; }
	
	void UpdatePrgRegister(uint8_t value) {
		_prgBank = value;
		SelectPrgPage(0, _prgBank & 0x3F);
		SetCpuMemoryMapping(0x6000, 0x7FFF, (_prgBank >> 6) & 0x3, PrgMemoryType::WorkRam, MemoryAccessType::ReadWrite);
	}
	
	void UpdateIrqSource() {
		if (_irqCounter == 0 && _romInfo.SubMapperID & 4) {
			_console->GetCpu()->SetIrqSource(IRQSource::External);
		} else {
			_console->GetCpu()->ClearIrqSource(IRQSource::External);
		}
	}
	
	void InitMapper() override
	{
		_flash.reset(new FlashSST39SF040(_prgRom, _prgSize));
		_orgPrgRom = vector<uint8_t>(_prgRom, _prgRom + _prgSize);
		
		for (int i = 0; i < 4; i++) {
			_chrBanks[i] = GetPowerOnByte();
		}

		_irqPrescaler = GetPowerOnByte();
		_irqCounter = GetPowerOnByte();
		
		UpdatePrgRegister(0);
		UpdateIrqSource();
		
		// Linear Mode
		if ( (_romInfo.SubMapperID & 3) == 0) {
			if (GetMirroringType() == MirroringType::FourScreens) {
				SetPpuMemoryMapping(0x0000, 0x3FFF, ChrMemoryType::ChrRam, 0x0000, MemoryAccessType::ReadWrite);
			} else {
				SetPpuMemoryMapping(0x0000, 0x1FFF, ChrMemoryType::ChrRam, 0x0000, MemoryAccessType::ReadWrite);
			}
		}
		// Shared Mode
		else if ( (_romInfo.SubMapperID & 3) == 1)
		{
			for (int i = 0; i < 4; i++) {
				SelectChrPage(i, _chrBanks[i] );
			}
			
			if (GetMirroringType() == MirroringType::FourScreens) {
				// Nametables
				SetPpuMemoryMapping(0x2000, 0x27FF, ChrMemoryType::ChrRam, 0x0000, MemoryAccessType::ReadWrite);
				SetPpuMemoryMapping(0x2800, 0x2FFF, ChrMemoryType::ChrRam, 0x7800, MemoryAccessType::ReadWrite);
				// Bonus Ram
				SetPpuMemoryMapping(0x3000, 0x37FF, ChrMemoryType::ChrRam, 0x0000, MemoryAccessType::ReadWrite);
				SetPpuMemoryMapping(0x3800, 0x3FFF, ChrMemoryType::ChrRam, 0x7800, MemoryAccessType::ReadWrite);
			}
		}
		// Independent Mode
		else if ( (_romInfo.SubMapperID & 3) == 2) 
		{
			_banksPerWindow = _chrRamSize/4/0x0800;

			for (int i = 0; i < 4; i++) {
				SelectChrPage(i, (_chrBanks[i]%_banksPerWindow) + (_banksPerWindow*i) );
			}
			
			if (GetMirroringType() == MirroringType::FourScreens) {
				// Nametables
				SetPpuMemoryMapping(0x2000, 0x27FF, ChrMemoryType::ChrRam, 0x7800, MemoryAccessType::ReadWrite);
				SetPpuMemoryMapping(0x2800, 0x2FFF, ChrMemoryType::ChrRam, 0xF800, MemoryAccessType::ReadWrite);
				// Bonus RAM
				SetPpuMemoryMapping(0x3000, 0x37FF, ChrMemoryType::ChrRam, 0x17800, MemoryAccessType::ReadWrite);
				SetPpuMemoryMapping(0x3800, 0x3FFF, ChrMemoryType::ChrRam, 0x1F800, MemoryAccessType::ReadWrite);
			}
		}
	}

	void Serialize(Serializer& s) override
	{
		BaseMapper::Serialize(s);

		SV(_flash);
		SV(_prgBank); SV(_irqPrescaler); SV(_irqCounter); SVArray(_chrBanks,4);
		
		SerializeRomDiff(s, _orgPrgRom);
	}

	void ApplySaveData()
	{
		if(_console->GetNesConfig().DisableFlashSaves) {
			return;
		}

		LoadRomPatch(_orgPrgRom);
	}

	void SaveBattery() override
	{
		if(_console->GetNesConfig().DisableFlashSaves) {
			return;
		}

		SaveRom(_orgPrgRom);
	}
	
	uint8_t ReadRegister(uint16_t addr) override
	{
		int16_t value = _flash->Read(addr);
		if(value >= 0) {
			return (uint8_t)value;
		}

		return BaseMapper::InternalReadRam(addr);
	}
	void WriteRegister(uint16_t addr, uint8_t value) override
	{
		_flash->Write( (addr & 0x7FFF) |	((_prgBank & 0x3F) << 15), value);

		switch (addr & 0xE000) {
			case 0x8000:
				UpdatePrgRegister(value);
				break;
			case 0xC000:
				if (_romInfo.SubMapperID & 4) {
					_irqCounter = value;
					UpdateIrqSource();
				}
				break;
			case 0xE000:
				if ( (_romInfo.SubMapperID & 3) == 1) {
					_chrBanks[addr&3] = value;
					SelectChrPage(addr&3, value);
				}
				else if ( (_romInfo.SubMapperID & 3) == 2) {
					_chrBanks[addr&3] = value;
					SelectChrPage(addr&3, (_chrBanks[addr&3]%_banksPerWindow) + (_banksPerWindow*(addr&3)) );
				}
				break;
			default:
				break;
		}
	}
	
	vector<MapperStateEntry> GetMapperStateEntries() override
	{
		vector<MapperStateEntry> entries;
		entries.push_back(MapperStateEntry("$8000.0-5", "PRG Bank", _prgBank & 0x3F, MapperStateValueType::Number8));
		entries.push_back(MapperStateEntry("$8000.6-7", "WRAM Bank", _prgBank >> 6, MapperStateValueType::Number8));
		if ( _romInfo.SubMapperID & 4 ) {
			entries.push_back(MapperStateEntry("$C000", "IRQ Timer", _irqCounter, MapperStateValueType::Number8));
			entries.push_back(MapperStateEntry("$C000", "IRQ Prescaler", _irqPrescaler, MapperStateValueType::Number8));
		}
		if ((_romInfo.SubMapperID & 3) != 0) {
			entries.push_back(MapperStateEntry("$E000", "CHR Bank 0", _chrBanks[0], MapperStateValueType::Number8));
			entries.push_back(MapperStateEntry("$E001", "CHR Bank 1", _chrBanks[1], MapperStateValueType::Number8));
			entries.push_back(MapperStateEntry("$E002", "CHR Bank 2", _chrBanks[2], MapperStateValueType::Number8));
			entries.push_back(MapperStateEntry("$E003", "CHR Bank 3", _chrBanks[3], MapperStateValueType::Number8));
		}
		return entries;
	}
	
	uint8_t MapperReadVram(uint16_t addr, MemoryOperationType memoryOperationType) override
	{
		if (addr < 0x2000) {
			_irqPrescaler = 0;
		} else {
			_irqPrescaler++;
			if ((_irqPrescaler & 0x7) == 0x4) {
				_irqCounter--;
				UpdateIrqSource();
			}
		}
		return InternalReadVram(addr);
	}
};