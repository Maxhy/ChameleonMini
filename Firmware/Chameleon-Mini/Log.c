#include "Log.h"
#include "Settings.h"
#include "Terminal/Terminal.h"
#include "System.h"
#include "Map.h"
#include "LEDHook.h"

static uint8_t LogMem[LOG_SIZE];
static uint8_t* LogMemPtr;
static uint16_t LogMemLeft;
static uint16_t LogFRAMAddr = FRAM_LOG_START_ADDR;
LogFuncType CurrentLogFunc;

static const MapEntryType PROGMEM LogModeMap[] = {
	{ .Id = LOG_MODE_OFF, 		.Text = "OFF" 		},
	{ .Id = LOG_MODE_MEMORY, 	.Text = "MEMORY" 	},
	{ .Id = LOG_MODE_LIVE, 	.Text = "LIVE" 	}
};

static void LogFuncOff(LogEntryEnum Entry, const void* Data, uint8_t Length)
{
    /* Do nothing */
}

static void LogFuncMemory(LogEntryEnum Entry, const void* Data, uint8_t Length)
{
	uint16_t SysTick = SystemGetSysTick();

    if (LogMemLeft >= (Length + 4)) {
        LogMemLeft -= Length + 4;

        uint8_t* DataPtr = (uint8_t*) Data;

        /* Write down Entry Id, Data length and Timestamp */
        *LogMemPtr++ = (uint8_t) Entry;
        *LogMemPtr++ = (uint8_t) Length;
        *LogMemPtr++ = (uint8_t) (SysTick >> 8);
        *LogMemPtr++ = (uint8_t) (SysTick >> 0);

        /* Write down data bytes */
        while(Length--) {
            *LogMemPtr++ = *DataPtr++;
        }
    } else {
        /* If memory full. Deactivate logmode */
        LogSetModeById(LOG_MODE_OFF);
        LEDHook(LED_LOG_MEM_FULL, LED_ON);
    }
}

static void LogFuncLive(LogEntryEnum Entry, const void* Data, uint8_t Length)
{
	uint16_t SysTick = SystemGetSysTick();

	TerminalSendByte((uint8_t) Entry);
	TerminalSendByte((uint8_t) Length);
	TerminalSendByte((uint8_t) (SysTick >> 8));
	TerminalSendByte((uint8_t) (SysTick >> 0));
	TerminalSendBlock(Data, Length);
}

void LogInit(void)
{
    LogMemClear();
    LogSetModeById(GlobalSettings.ActiveSettingPtr->LogMode);
}

void LogTick(void)
{

}

void LogTask(void)
{

}

bool LogMemLoadBlock(void* Buffer, uint32_t BlockAddress, uint16_t ByteCount)
{
	if (BlockAddress < sizeof(LogMem) + (LogFRAMAddr - FRAM_LOG_START_ADDR))
	{
		bool overflow = false;
		uint16_t remainderByteCount = 0;
		// prevent buffer overflows:
		if ((BlockAddress + ByteCount) >= sizeof(LogMem) + (LogFRAMAddr - FRAM_LOG_START_ADDR))
		{
			overflow = true;
			uint16_t tmp = sizeof(LogMem) + (LogFRAMAddr - FRAM_LOG_START_ADDR) - BlockAddress;
			remainderByteCount = ByteCount - tmp;
			ByteCount = tmp;
		}

		/*
		 * 1. case: The whole block is in FRAM.
		 * 2. case: The block wraps from FRAM to SRAM
		 * 3. case: The whole block is in SRAM.
		 */
		if (BlockAddress < (LogFRAMAddr - FRAM_LOG_START_ADDR) && (BlockAddress + ByteCount) < (LogFRAMAddr - FRAM_LOG_START_ADDR))
		{
			MemoryReadBlock(Buffer, BlockAddress + FRAM_LOG_START_ADDR, ByteCount);
		} else if (BlockAddress < (LogFRAMAddr - FRAM_LOG_START_ADDR)) {
			uint16_t FramByteCount = LogFRAMAddr - (BlockAddress + FRAM_LOG_START_ADDR);
			MemoryReadBlock(Buffer, BlockAddress + FRAM_LOG_START_ADDR, FramByteCount);
			memcpy(Buffer + FramByteCount, LogMem, ByteCount - FramByteCount);
		} else {
			memcpy(Buffer, LogMem + BlockAddress - LogFRAMAddr, ByteCount);
		}

		if (overflow)
		{
			while (remainderByteCount--)
				((uint8_t*) Buffer)[ByteCount + remainderByteCount] = 0x00;
		}

		return true;
	} else {
		return false;
	}
}

void LogMemClear(void)
{
    uint16_t i;

    for (i=0; i<sizeof(LogMem); i++) {
        LogMem[i] = (uint8_t) LOG_EMPTY;
    }

    LogMemPtr = LogMem;
    LogMemLeft = sizeof(LogMem);
    LogFRAMAddr = FRAM_LOG_START_ADDR;
    LEDHook(LED_LOG_MEM_FULL, LED_OFF);
}

uint16_t LogMemFree(void)
{
    return LogMemLeft;
}


void LogSetModeById(LogModeEnum Mode)
{
#ifndef LOG_SETTING_GLOBAL
    GlobalSettings.ActiveSettingPtr->LogMode = Mode;
#else
	/* Write Log settings globally */
	for (uint8_t i=0; i<SETTINGS_COUNT; i++) {
		 GlobalSettings.Settings[i].LogMode = Mode;
	}
#endif

    switch(Mode) {
    case LOG_MODE_OFF:
        CurrentLogFunc = LogFuncOff;
        break;

    case LOG_MODE_MEMORY:
        CurrentLogFunc = LogFuncMemory;
        break;

    case LOG_MODE_LIVE:
        CurrentLogFunc = LogFuncLive;
        break;

    default:
        break;
    }

}

bool LogSetModeByName(const char* Mode)
{
	MapIdType Id;

	if (MapTextToId(LogModeMap, ARRAY_COUNT(LogModeMap), Mode, &Id)) {
		LogSetModeById(Id);
		return true;
	}

    return false;
}

void LogGetModeByName(char* Mode, uint16_t BufferSize)
{
	MapIdToText(LogModeMap, ARRAY_COUNT(LogModeMap),
			GlobalSettings.ActiveSettingPtr->LogMode, Mode, BufferSize);
}

void LogGetModeList(char* List, uint16_t BufferSize)
{
    MapToString(LogModeMap, ARRAY_COUNT(LogModeMap), List, BufferSize);
}

void LogSRAMToFRAM(void)
{
	if (LogMemLeft < sizeof(LogMem))
	{
		if (FRAM_LOG_SIZE - (LogFRAMAddr - FRAM_LOG_START_ADDR) >= LOG_SIZE - LogMemLeft)
		{
			MemoryWriteBlock(LogMem, LogFRAMAddr, LOG_SIZE - LogMemLeft);
			uint16_t tmp = LogFRAMAddr + (LOG_SIZE - LogMemLeft);
			LogMemClear(); // this function also resets LogFRAMAddr, but does not delete anything from FRAM, so we just save the new FRAM address
			LogFRAMAddr = tmp;
		} else {
			// TODO handle the case in which the FRAM is full
		}
	}
}
