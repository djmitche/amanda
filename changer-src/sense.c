#include <amanda.h>

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "scsi-defs.h" 
/*
 Handling of Sense codes which are returned from the device
 At the moment the following status us returned
 SENSE_ABORT	       	-> -1 , some strange happend, abort everything
 SENSE_IGNORE	       	-> 0 , nothing special, only info
 SENSE_NO_TAPE	       	-> 1 , this is for tape devices, not tape online
 SENSE_RETRY	       	-> 2 , retry the command
 SENSE_IES	       	-> 3 , initialize element status
 SENSE_TAPE_NOT_LOADED 	-> 4 , no tape loaded
 SENSE_NO              	-> 5 , no sense information
 SENSE_TAPE_NOT_UNLOADED -> 6 , tape is not ejected
 SENSE_IES		-> FF , Sense from initialize element status
 */

	SenseType_T SenseType [] = {
/*
 * Generic one, this is used if not information is found based on the ident of the device
 */
	{ "generic", "", TYPE_TAPE,  SENSE_NULL, 0x0, 0x0, SENSE_NO, "No Sense"},
	{ "generic", "", TYPE_TAPE,  SENSE_NULL , -1, -1, SENSE_RETRY, "Default for SENSE_NULL"},

	{ "generic", "", TYPE_TAPE , SENSE_RECOVERED_ERROR, 0x0, 0x0, SENSE_IGNORE, "Recovered Error"},
	{ "generic", "", TYPE_TAPE,  SENSE_RECOVERED_ERROR , -1, -1, SENSE_RETRY, "Default for SENSE_RECOVERED_ERROR"},

	{ "generic", "", TYPE_TAPE , SENSE_NOT_READY, 0x0, 0x0, SENSE_IGNORE, "Not Ready"},
	{ "generic", "", TYPE_TAPE , SENSE_NOT_READY, 0x3A, 0x0, SENSE_TAPE_NOT_ONLINE, "No Tape online"},
	{ "generic", "", TYPE_TAPE,  SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "generic", "", TYPE_TAPE , SENSE_MEDIUM_ERROR, 0x0, 0x0, SENSE_ABORT, "Medium Error"},
	{ "generic", "", TYPE_TAPE,  SENSE_MEDIUM_ERROR , -1, -1, SENSE_ABORT, "Default for SENSE_MEDIUM_ERROR"},

	{ "generic", "", TYPE_TAPE , SENSE_HARDWARE_ERROR, 0x0, 0x0, SENSE_ABORT, "Hardware Error"},
	{ "generic", "", TYPE_TAPE,  SENSE_HARDWARE_ERROR , -1, -1, SENSE_ABORT, "Default for SENSE_HARDWARE_ERROR"},

	{ "generic", "", TYPE_TAPE , SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "generic", "", TYPE_TAPE,  SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},

	{ "generic", "", TYPE_TAPE , SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "generic", "", TYPE_TAPE,  SENSE_UNIT_ATTENTION, -1, -1, SENSE_ABORT, "Default for SENSE_UNIT_ATTENTION"},

	{ "generic", "", TYPE_TAPE , -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found"},

	{ "generic", "", TYPE_CHANGER,  SENSE_NULL, 0x0, 0x0, SENSE_NO, "No Sense"},
	{ "generic", "", TYPE_CHANGER,  SENSE_NULL , -1, -1, SENSE_RETRY, "Default for SENSE_NULL"},

	{ "generic", "", TYPE_CHANGER , SENSE_RECOVERED_ERROR, 0x0, 0x0, SENSE_IGNORE, "Recovered Error"},
	{ "generic", "", TYPE_CHANGER,  SENSE_RECOVERED_ERROR , -1, -1, SENSE_RETRY, "Default for SENSE_RECOVERED_ERROR"},

	{ "generic", "", TYPE_CHANGER , SENSE_NOT_READY, 0x0, 0x0, SENSE_IGNORE, "Not Ready"},
	{ "generic", "", TYPE_CHANGER , SENSE_NOT_READY, 0x3A, 0x0, SENSE_TAPE_NOT_ONLINE, "No Tape online"},
	{ "generic", "", TYPE_CHANGER,  SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "generic", "", TYPE_CHANGER , SENSE_MEDIUM_ERROR, 0x0, 0x0, SENSE_ABORT, "Medium Error"},
	{ "generic", "", TYPE_CHANGER,  SENSE_MEDIUM_ERROR , -1, -1, SENSE_ABORT, "Default for SENSE_MEDIUM_ERROR"},

	{ "generic", "", TYPE_CHANGER , SENSE_HARDWARE_ERROR, 0x0, 0x0, SENSE_ABORT, "Hardware Error"},
	{ "generic", "", TYPE_CHANGER,  SENSE_HARDWARE_ERROR , -1, -1, SENSE_ABORT, "Default for SENSE_HARDWARE_ERROR"},

	{ "generic", "", TYPE_CHANGER , SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "generic", "", TYPE_CHANGER,  SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},

	{ "generic", "", TYPE_CHANGER , SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "generic", "", TYPE_CHANGER,  SENSE_UNIT_ATTENTION, -1, -1, SENSE_ABORT, "Default for SENSE_UNIT_ATTENTION"},

	{ "generic", "", TYPE_CHANGER , -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found"},
/*
 * HP C1553A Tape
 */
	{ "C1553A", "", TYPE_TAPE,  SENSE_NULL, 0x0, 0x0, SENSE_NO, "No Sense"},
	{ "C1553A", "", TYPE_TAPE,  SENSE_NULL , -1, -1, SENSE_RETRY, "Default for SENSE_NULL"},

	{ "C1553A", "", TYPE_TAPE , SENSE_RECOVERED_ERROR, 0x0, 0x0, SENSE_IGNORE, "Recovered Error"},
	{ "C1553A", "", TYPE_TAPE,  SENSE_RECOVERED_ERROR , -1, -1, SENSE_RETRY, "Default for SENSE_RECOVERED_ERROR"},

	{ "C1553A", "", TYPE_TAPE , SENSE_NOT_READY, 0x0, 0x0, SENSE_IGNORE, "Not Ready"},
	{ "C1553A", "", TYPE_TAPE , SENSE_NOT_READY, 0x3A, 0x0, SENSE_TAPE_NOT_ONLINE, "No Tape online"},
	{ "C1553A", "", TYPE_TAPE,  SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "C1553A", "", TYPE_TAPE , SENSE_MEDIUM_ERROR, 0x0, 0x0, SENSE_ABORT, "Medium Error"},
	{ "C1553A", "", TYPE_TAPE,  SENSE_MEDIUM_ERROR , -1, -1, SENSE_ABORT, "Default for SENSE_MEDIUM_ERROR"},

	{ "C1553A", "", TYPE_TAPE , SENSE_HARDWARE_ERROR, 0x0, 0x0, SENSE_ABORT, "Hardware Error"},
	{ "C1553A", "", TYPE_TAPE,  SENSE_HARDWARE_ERROR , -1, -1, SENSE_ABORT, "Default for SENSE_HARDWARE_ERROR"},

	{ "C1553A", "", TYPE_TAPE , SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "C1553A", "", TYPE_TAPE,  SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},

	{ "C1553A", "", TYPE_TAPE , SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "C1553A", "", TYPE_TAPE,  SENSE_UNIT_ATTENTION , -1, -1, SENSE_RETRY, "Default for SENSE_UNIT_ATTENTION"},

	{ "C1553A", "", TYPE_TAPE , -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found"},

	{ "C1553A", "", TYPE_CHANGER,  SENSE_NULL, 0x0, 0x0, SENSE_NO, "No Sense"},
	{ "C1553A", "", TYPE_CHANGER,  SENSE_NULL , -1, -1, SENSE_RETRY, "Default for SENSE_NULL"},

	{ "C1553A", "", TYPE_CHANGER , SENSE_RECOVERED_ERROR, 0x0, 0x0, SENSE_IGNORE, "Recovered Error"},
	{ "C1553A", "", TYPE_CHANGER,  SENSE_RECOVERED_ERROR , -1, -1, SENSE_RETRY, "Default for SENSE_RECOVERED_ERROR"},

	{ "C1553A", "", TYPE_CHANGER , SENSE_NOT_READY, 0x0, 0x0, SENSE_IGNORE, "Not Ready"},
	{ "C1553A", "", TYPE_CHANGER , SENSE_NOT_READY, 0x3A, 0x0, SENSE_TAPE_NOT_ONLINE, "No Tape online"},
	{ "C1553A", "", TYPE_CHANGER,  SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "C1553A", "", TYPE_CHANGER , SENSE_MEDIUM_ERROR, 0x0, 0x0, SENSE_ABORT, "Medium Error"},
	{ "C1553A", "", TYPE_CHANGER,  SENSE_MEDIUM_ERROR , -1, -1, SENSE_ABORT, "Default for SENSE_MEDIUM_ERROR"},

	{ "C1553A", "", TYPE_CHANGER , SENSE_HARDWARE_ERROR, 0x0, 0x0, SENSE_ABORT, "Hardware Error"},
	{ "C1553A", "", TYPE_CHANGER,  SENSE_HARDWARE_ERROR , -1, -1, SENSE_ABORT, "Default for SENSE_HARDWARE_ERROR"},

	{ "C1553A", "", TYPE_CHANGER , SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "C1553A", "", TYPE_CHANGER,  SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},
	
	{ "C1553A", "", TYPE_CHANGER , SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "C1553A", "", TYPE_CHANGER,  SENSE_UNIT_ATTENTION , -1, -1, SENSE_RETRY, "Default for SENSE_UNIT_ATTENTION"},
	
	{ "C1553A", "", TYPE_CHANGER , -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found"},
/*
 * Tandberg Tape
 */
	{ "TDS 1420", "", TYPE_TAPE, SENSE_NULL, 0x0, 0x0, SENSE_NO, "No Sense"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_NULL , -1, -1, SENSE_RETRY, "Default for SENSE_NULL"},

	{ "TDS 1420", "", TYPE_TAPE, SENSE_IES, 0x83, 0x0, SENSE_IES, "IES"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_IES, 0x83, 0x1, SENSE_IES, "IES"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_IES, 0x83, 0x4, SENSE_IGNORE, "IES"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_RECOVERED_ERROR, 0x0, 0x0, SENSE_IGNORE, "Recovered Error"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_RECOVERED_ERROR , -1, -1, SENSE_RETRY, "Default for SENSE_RECOVERED_ERROR"},

	{ "TDS 1420", "", TYPE_TAPE, SENSE_NOT_READY, 0x0, 0x0, SENSE_IGNORE, "Not Ready"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_NOT_READY, 0x3A, 0x0, SENSE_TAPE_NOT_ONLINE, "No Tape online"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "TDS 1420", "", TYPE_TAPE, SENSE_MEDIUM_ERROR, 0x0, 0x0, SENSE_ABORT, "Medium Error"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_MEDIUM_ERROR , -1, -1, SENSE_ABORT, "Default for SENSE_MEDIUM_ERROR"},

	{ "TDS 1420", "", TYPE_TAPE, SENSE_HARDWARE_ERROR, 0x0, 0x0, SENSE_ABORT, "Hardware Error"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_HARDWARE_ERROR , -1, -1, SENSE_ABORT, "Default for SENSE_HARDWARE_ERROR"},

	{ "TDS 1420", "", TYPE_TAPE, SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},
	
	{ "TDS 1420", "", TYPE_TAPE, SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "TDS 1420", "", TYPE_TAPE, SENSE_UNIT_ATTENTION , -1, -1, SENSE_RETRY, "Default for SENSE_UNIT_ATTENTION"},
	
	{ "TDS 1420", "", TYPE_TAPE, -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found"},
/*
 * DLT 7000 Tape
 */
	{ "DLT7000", "", TYPE_TAPE, SENSE_NOT_READY, 0x4, 0x0, SENSE_RETRY, "Logical Unit not ready, no additionla sense"},
	{ "DLT7000", "", TYPE_TAPE, SENSE_NOT_READY, 0x4, 0x2, SENSE_TAPE_NOT_ONLINE, "Logical Unit not ready, in progress becoming ready"},
	{ "DLT7000", "", TYPE_TAPE, SENSE_NOT_READY, 0x30, 0x3, SENSE_RETRY, "The tape drive is being cleaned"},
	{ "DLT7000", "", TYPE_TAPE, SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "DLT7000", "", TYPE_TAPE, SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "DLT7000", "", TYPE_TAPE, SENSE_UNIT_ATTENTION , -1, -1, SENSE_RETRY, "Default for SENSE_UNIT_ATTENTION"},
	
	{ "DLT7000", "", TYPE_TAPE, SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "DLT7000", "", TYPE_TAPE, SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},
	
	{ "DLT7000", "", TYPE_TAPE, -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found"},
/*
 * DLT 4000 Tape
 */
	{ "DLT4000", "", TYPE_TAPE, SENSE_NOT_READY, 0x4, 0x0, SENSE_RETRY, "Logical Unit not ready, no additionla sense"},
	{ "DLT4000", "", TYPE_TAPE, SENSE_NOT_READY, 0x4, 0x2, SENSE_TAPE_NOT_ONLINE, "Logical Unit not ready, in progress becoming ready"},
	{ "DLT4000", "", TYPE_TAPE, SENSE_NOT_READY, 0x30, 0x3, SENSE_RETRY, "The tape drive is being cleaned"},
	{ "DLT4000", "", TYPE_TAPE,  SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "DLT4000", "", TYPE_TAPE, SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "DLT4000", "", TYPE_TAPE, SENSE_UNIT_ATTENTION , -1, -1, SENSE_RETRY, "Default for SENSE_UNIT_ATTENTION"},
	
	{ "DLT4000", "", TYPE_TAPE, SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "DLT4000", "", TYPE_TAPE, SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},
	
	{ "DLT4000", "", TYPE_TAPE, -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found"},
/*
 * AIT VLS DLT Library
 */
	{ "VLS_DLT", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x0, SENSE_RETRY, "Logical Unit not ready, no additionla sense"},
	{ "VLS_DLT", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x2, SENSE_TAPE_NOT_ONLINE, "Logical Unit not ready, in progress becoming ready"},
	{ "VLS_DLT", "", TYPE_CHANGER, SENSE_NOT_READY, 0x30, 0x3, SENSE_RETRY, "The tape drive is being cleaned"},
	{ "VLS_DLT", "", TYPE_CHANGER, SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "VLS_DLT", "", TYPE_CHANGER, SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "VLS_DLT", "", TYPE_CHANGER, SENSE_UNIT_ATTENTION , -1, -1, SENSE_RETRY, "Default for SENSE_UNIT_ATTENTION"},
	
	{ "VLS_DLT", "", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "VLS_DLT", "", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},
	
	{ "VLS_DLT", "", TYPE_CHANGER, -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found"},
/*
 * AIT VLS SDX Library
 */
	{ "VLS_SDX", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x0, SENSE_RETRY, "Logical Unit not ready, no additionla sense"},
	{ "VLS_SDX", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x2, SENSE_TAPE_NOT_ONLINE, "Logical Unit not ready, in progress becoming ready"},
	{ "VLS_SDX", "", TYPE_CHANGER, SENSE_NOT_READY, 0x30, 0x3, SENSE_RETRY, "The tape drive is being cleaned"},
	{ "VLS_SDX", "", TYPE_CHANGER, SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "VLS_SDX", "", TYPE_CHANGER, SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "VLS_SDX", "", TYPE_CHANGER, SENSE_UNIT_ATTENTION , -1, -1, SENSE_RETRY, "Default for SENSE_UNIT_ATTENTION"},
	
	{ "VLS_SDX", "", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "VLS_SDX", "", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},
	
	{ "VLS_SDX", "", TYPE_CHANGER, -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found"},
/*
 * Exabyte 85058 Tape
 */
	{ "EXB-85058HE-0000", "", TYPE_TAPE, SENSE_NOT_READY, 0x4, 0x0, SENSE_RETRY, "Logical Unit not ready, no additionla sense"},
	{ "EXB-85058HE-0000", "", TYPE_TAPE, SENSE_NOT_READY, 0x4, 0x1, SENSE_RETRY, "Logical Unit not ready, in progress becoming ready"},
	{ "EXB-85058HE-0000", "", TYPE_TAPE, SENSE_NOT_READY, 0x30, 0x3, SENSE_RETRY, "The tape drive is being cleaned"},
	{ "EXB-85058HE-0000", "", TYPE_TAPE, SENSE_NOT_READY, 0x3A, 0x0, SENSE_TAPE_NOT_ONLINE, "No Tape online"},
	{ "EXB-85058HE-0000", "", TYPE_TAPE,  SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "EXB-85058HE-0000", "", TYPE_TAPE, SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "EXB-85058HE-0000", "", TYPE_TAPE, SENSE_UNIT_ATTENTION , -1, -1, SENSE_RETRY, "Default for SENSE_UNIT_ATTENTION"},
	
	{ "EXB-85058HE-0000", "", TYPE_TAPE, SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "EXB-85058HE-0000", "", TYPE_TAPE, SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},
	{ "EXB-85058HE-0000", "", TYPE_TAPE, -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found"},
/*
 * Exabyte 10e Library (Robot)
 */
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NULL, 0x0, 0x0, SENSE_RETRY, "Retry, no sense"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NULL, 0x90, 0x2, SENSE_ABORT, "Illegal Request"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NULL , 0x90, 0x3, SENSE_IES, "IES"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NULL , -1, -1, SENSE_RETRY, "Default for SENSE_NULL"},

	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x0, SENSE_RETRY, "Logical Unit not ready, no additionla sense"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x1, SENSE_RETRY, "Logical Unit not ready, in progress becoming ready"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x85, SENSE_ABORT, "Library door is open"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x85, SENSE_ABORT, "The data cartridge magazine is missing"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x89, SENSE_ABORT, "The library is in CHS Monitor mode"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x8C, SENSE_RETRY, "The library is performing a power-on self test"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x8D, SENSE_ABORT, "The library is in LCD mode"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x8E, SENSE_ABORT, "The library is in Sequential mode"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY, 0x30, 0x3, SENSE_RETRY, "The tape drive is being cleaned"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY, 0x3A, 0x0, SENSE_TAPE_NOT_ONLINE, "No Tape online"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_NOT_READY , -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},

	{ "EXB-10e", "", TYPE_CHANGER, SENSE_UNIT_ATTENTION, 0x0, 0x0, SENSE_RETRY, "Unit Attention"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_UNIT_ATTENTION , -1, -1, SENSE_RETRY, "Default for SENSE_UNIT_ATTENTION"},
	
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x0, 0x0, SENSE_ABORT, "Illegal Request"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x91, 0x0, SENSE_CHM_FULL, "CHM full during reset"},
	{ "EXB-10e", "", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST , -1, -1, SENSE_RETRY, "Default for SENSE_ILLEGAL_REQUEST"},
	
	{ "EXB-10e", "", TYPE_CHANGER, -1, 0x0, 0x0, SENSE_ABORT, "Nothing Found for EXB-10e"},
/*
 * Spectra TreeFrog  library
 */
 	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_NULL, 0x0, 0x0, SENSE_NO, "No Sense, Unit Ready"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_NULL, -1, -1, SENSE_NO, "No Sense, Unit Ready"},
	
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x0, SENSE_RETRY, "Unit Not Ready"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x1, SENSE_RETRY, "Unit is Becoming Ready"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_NOT_READY, 0x4, 0x83, SENSE_ABORT, "Door is open, Robot is Disabled"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_NOT_READY, -1, -1, SENSE_RETRY, "Default for SENSE_NOT_READY"},
	
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x4C, 0x0, SENSE_ABORT, "Unit Failed Initialization"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x84, 0x4, SENSE_ABORT, "DRAM Memory Failure"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x84, 0x4, SENSE_ABORT, "Two ore More SCSI ID's in the library are tehe same"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x81, 0x4, SENSE_ABORT, "Tape may be broken;of tape is a cleaning tape;or Drive B is broken"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x87, 0x0, SENSE_ABORT, "Bad FPROM or invalid device in socket"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x87, 0x1, SENSE_ABORT, "FPROM Erase Operation Failed"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x87, 0x2, SENSE_ABORT, "FFPROM Write Operation Failed"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x85, 0x1, SENSE_ABORT, "Robot not Initialized"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x85, 0x99, SENSE_ABORT, "Generic Robotics  Error"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x85, 0x2, SENSE_ABORT, "Long Axis Robotics Error"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x85, 0x3, SENSE_ABORT, "Short Axis Robotics Error"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, 0x85, 0x4, SENSE_ABORT, "Ambient Light Detected"},
	{ "215", "SPECTRA", TYPE_CHANGER, SENSE_HARDWARE_ERROR, -1, -1, SENSE_ABORT, "Default for SENSE_HARDWARE_ERROR"},
	
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x1A, 0x0, SENSE_ABORT, "Parameter List Length Error"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x20, 0x0, SENSE_ABORT, "Invalid Command Code"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x21, 0x01, SENSE_ABORT, "Invalid Element Address"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x24, 0x0, SENSE_ABORT, "Invalid Field in CDB"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x25, 0x0, SENSE_ABORT, "LUN Not Supported"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x26, 0x0, SENSE_ABORT, "Invalid Parameter Field"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x3b, 0xd, SENSE_ABORT, "Medium Destination is full"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x3b, 0xe, SENSE_ABORT, "Medium Source Element is Full"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x3d, 0x80, SENSE_ABORT, "Disconnects Must be Allowed"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x80, 0x18, SENSE_ABORT, "Conflict, Element is Reserved"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x81, 0x2, SENSE_ABORT, "Library is Full of Tapes"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, 0x81, 0x3, SENSE_ABORT, "Grip Arm not Empty"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_ILLEGAL_REQUEST, -1 , -1, SENSE_ABORT, "Default for SENSE_ILLEGAL_REQUEST"},
	
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_UNIT_ATTENTION, 0x28, 0x0, SENSE_IES, "Inventory possible Altered"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_UNIT_ATTENTION, 0x29, 0x0, SENSE_RETRY, "A Reset Has Occured"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_UNIT_ATTENTION, 0x24, 0x1, SENSE_IGNORE, "Mode Parameter Have CHanged"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_UNIT_ATTENTION, -1, -1, SENSE_IGNORE, "Default for SENSE_UNIT_ATTENTION"},
	
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_VENDOR_SPECIFIC, 0x83, 0x00, SENSE_ABORT, "Barcode Label is Unread"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_VENDOR_SPECIFIC, 0x83, 0x01, SENSE_ABORT, "Problem Reading Barcode"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_VENDOR_SPECIFIC, 0x83, 0x11, SENSE_ABORT, "Tape in Drive & Unmounted"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_VENDOR_SPECIFIC, 0x84, 0x00, SENSE_ABORT, "Unsupported SCSI Command"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_VENDOR_SPECIFIC, 0x84, 0x01, SENSE_ABORT, "No Response from SCSI Target"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_VENDOR_SPECIFIC, 0x84, 0x02, SENSE_ABORT, "Check Condition form Target"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_VENDOR_SPECIFIC, 0x84, 0x03, SENSE_ABORT, "SCSI ID Same as Library's ID"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_VENDOR_SPECIFIC, 0x84, 0x08,  SENSE_ABORT, "Busy Condition from Target"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_VENDOR_SPECIFIC, 0x84, 0x18, SENSE_ABORT, "SCSI Reservation Conflict"},
	{"215", "SPECTRA", TYPE_CHANGER, SENSE_VENDOR_SPECIFIC,  -1, -1, SENSE_ABORT, "Default for SENSE_VENDOR_SPECIFIC"},
	 
	{ "215", "SPECTRA", TYPE_CHANGER, -1, 0x0, 0x0, SENSE_ABORT, "Nothing found for Spectra/215"},
	
	{ NULL, "", 0x0, -1, 0x0, 0x0, 0x0, ""},

	};


void DumpSense()
{
	SenseType_T *pwork = (SenseType_T *)&SenseType;

	while (pwork->ident != NULL)	
	{
		if (pwork->sense == -1)
		{
			printf("\n");
		} else {
			printf("%s %s %02X %02X %02X %d %s\n",pwork->ident, pwork->vendor,
				pwork->sense,
				pwork->asc,
				pwork->ascq,
				pwork->ret,
				pwork->text);
		}
		pwork++;
	}
}

/*
 * Decode the Sense Key,ASC, ASCQ and device type, and return an action Type.
 *	type describes the device type as returned by the inquiry command
 *	ignsense changes the way the ASC/ASCQ values are handled.
 *	 if = 0 the sense key is also used for decoding,
 *	 if > 0 the sense key is ignored in the search, and only ASC/ASCQ will be checked
 *	sense is the sense key returned by the scsi command
 *	ASC is the ASC value returned by the scsi command
 *	ASCQ is the ASCQ value returned by the scsi command
 *	text is a pointer to store the clear text reason from the table.
 *	
 *	TODO:
 *	
*/
int Sense2Action(char *ident,
		unsigned char type,
		unsigned char ignsense,
		unsigned char sense,
		unsigned char asc,
		unsigned char ascq,
		char **text)
{
	/*
	 * Decode the combination of sense key, ASC, ASCQ and return what to do with this
	 * status
	 * A future extension could be to call direct a function which handles this case
	 *
	 */
	SenseType_T *pwork = (SenseType_T *)&SenseType;
	SenseType_T *generic = NULL;
        int in = 0;

	dbprintf(("Sense2Action START : type(%d), ignsense(%d), sense(%02X), asc(%02X), ascq(%02X)\n",
		type,
		ignsense,
		sense,
		asc,
		ascq));
	
	while (pwork->ident != NULL)	
	{
		if (strcmp(pwork->ident, "generic") == 0 && generic == NULL)
		{
			generic = pwork;
		}
		
		if (strcmp(pwork->ident, ident) == 0 && pwork->type == type)
                {
			in = 1;
                } else {
			if (in == 1)
			{
				dbprintf(("Sense2Action       : no match\n"));
				break;
			}
			pwork++;
			continue;
		}

		if (in == 1)
		{
			/* End of definitions for this device */
			if (pwork->sense == -1)
			{
				*text = (char *)strdup(pwork->text);
				dbprintf(("Sense2Action   END : no match for %s %s\n",
					pwork->ident,
					pwork->vendor));
				return(pwork->ret);
			}
			
			if (ignsense)
			{
				if (pwork->asc ==  asc && pwork->ascq == ascq)
				{
					*text = (char *)strdup(pwork->text);
					dbprintf(("Sense2Action END(IGN) : match for %s %s  return -> %d/%s\n",
						pwork->ident,
						pwork->vendor,
						pwork->ret,
						*text));	
					return(pwork->ret);
				}
				pwork++;
				continue;
			} 
			
			if (pwork->sense == sense)
			{
				/* Matching ASC/ASCQ, if yes return the defined result code */
				if (pwork->asc ==  asc && pwork->ascq == ascq)
				{
					*text = (char *)strdup(pwork->text);
					dbprintf(("Sense2Action   END : match for %s %s  return -> %d/%s\n",
						pwork->ident,
						pwork->vendor,
						pwork->ret,
						*text));		
					return(pwork->ret);
				}
				
				/* End of definitions for this sense type, if yes return the default
				 * for this type
				 */
				if ( 	pwork->asc == -1 && pwork->ascq == -1)
				{
					*text = (char *)strdup(pwork->text);
					dbprintf(("Sense2Action   END : no match for %s %s  return -> %d/%s\n",
						pwork->ident,
						pwork->vendor,
						pwork->ret,
						*text));	
					return(pwork->ret);
				}			
				pwork++;
				continue;
			}
			
			pwork++;
			continue;
		}
		
		pwork++;
	}

	/* 
	 * Ok no match found, so lets return the values from the generic table
	 */
	dbprintf(("Sense2Action generic start :\n"));
	while (generic->ident != NULL)
	{
		if (generic->sense == -1)
		{
			*text = (char *)strdup(generic->text);
			dbprintf(("Sense2Action generic END : match for %s %s  return -> %d/%s\n",
				generic->ident,
				generic->vendor,
				generic->ret,
				*text));	
			return(generic->ret);
		}
		
		if (ignsense)
		{
			if (generic->asc ==  asc && generic->ascq == ascq)
			{
				*text = (char *)strdup(generic->text);
				dbprintf(("Sense2Action generic END(IGN) : match for %s %s  return -> %d/%s\n",
					generic->ident,
					generic->vendor,
					generic->ret,
					*text));	
				return(generic->ret);
			}
			generic++;
			continue;
		} 
			
		if (generic->sense == sense)
		{
			/* Matching ASC/ASCQ, if yes return the defined result code */
			if (generic->asc ==  asc && generic->ascq == ascq)
			{
				*text = (char *)strdup(generic->text);
				dbprintf(("Sense2Action generic END : match for %s %s  return -> %d/%s\n",
					generic->ident,
					generic->vendor,
					generic->ret,
					*text));	
				return(generic->ret);
			}
			
			/* End of definitions for this sense type, if yes return the default
			 * for this type
			 */
			if ( 	generic->asc == -1 && generic->ascq == -1)
			{
				*text = (char *)strdup(generic->text);
				dbprintf(("Sense2Action generic END : no match for %s %s  return -> %d/%s\n",
					generic->ident,
					generic->vendor,
					generic->ret,
					*text));	
				return(generic->ret);
			}			
			generic++;
			continue;
		}
		generic++;
	}	

	dbprintf(("Sense2Action END:\n"));
	*text = (char *)strdup("No match found");
	return(SENSE_ABORT);
}
