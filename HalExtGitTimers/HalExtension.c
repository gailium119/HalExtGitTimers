/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License.

Module Name:

    HalExtension.c

Abstract:

    This file implements a HAL Extension Module for the ARM Git Timers.

*/

//
// ------------------------------------------------------------------- Includes
//

#include "minhalext.h"
#include <intrin.h>

//
// ---------------------------------------------------------------- Definitions
//

//
// Timer control constants
//

typedef enum _GIT_TIMER_TYPE
{
    GitTimerInvalid = 0x0,
    GitTimerCounter = 0x1,
    GitTimerInterrupts = 0x2,
} GIT_TIMER_TYPE;

//
// ------------------------------------------------- Data Structure Definitions
//

#pragma pack(push, 1)


//
// CSRT Timer descriptor
// Matches the CSRT resource descriptor (RD_TIMER).
//

typedef struct _CSRT_RESOURCE_DESCRIPTOR_TIMER_INSTANCE {

    CSRT_RESOURCE_DESCRIPTOR_HEADER Header;

    UINT32 TimerType;
    UINT32 ClockSource;
    UINT32 Frequency;
    UINT32 FrequencyScale;
    UINT32 BaseAddress;
    UINT32 Interrupt;

} CSRT_RESOURCE_DESCRIPTOR_TIMER_INSTANCE, * PCSRT_RESOURCE_DESCRIPTOR_TIMER_INSTANCE;

//
// Git runtime timer internal data, that is
// passed by the framework to each timer related callback routine.
//

typedef struct _GIT_INTERNAL_DATA
{
    GIT_TIMER_TYPE Type;
    TIMER_MODE Mode;
    ULONGLONG LastCompareValue;
    ULONGLONG PeriodicInterval;
} GIT_INTERNAL_DATA, * PGIT_INTERNAL_DATA;

typedef struct _EGIT_TABLE
{
    DESCRIPTION_HEADER Header;
    ULONGLONG MemoryMappedPhysicalAddress;
    ULONG SecurePhysicalTimerGsiv;
    ULONG NonSecurePhysicalTimerGsiv;
    ULONG VirtualTimerEventGsiv;
    ULONG HypervisorTimerEventGsiv;
    UCHAR SecurePhysicalTimerFlags;
    UCHAR NonSecurePhysicalTimerFlags;
    UCHAR VirtualTimerEventFlags;
    UCHAR HypervisorTimerEventFlags;
    ULONG GlobalFlags;
} EGIT_TABLE, * PEGIT_TABLE;

#pragma pack(pop)

//
// ----------------------------------------------- Global Variables
//

static ULONG g_GitFrequency = 0; // Global Variable that stores the Git timer frequency

static ULONG g_WindowsBuildNumber = 0; // Global Variable that stores the Windows build number

//
// ----------------------------------------------- Internal Function Prototypes
//

NTSTATUS GitInitialize(
    __in PVOID TimerDataPtr
);
ULONGLONG GitQueryCounter(
    __in_opt PVOID TimerDataPtr
);
VOID GitSetTimerCompareValue(
    __in ULONGLONG CompareValue
);
NTSTATUS GitArmTimer(
    __in PVOID TimerDataPtr,
    __in TIMER_MODE Mode,
    __in ULONGLONG TickCount
);
VOID GitAcknowledgeInterrupt(
    __in PVOID TimerDataPtr
);
VOID GitStop(
    __in PVOID TimerDataPtr
);
NTSTATUS GitDiscover(
    __in ULONG Handle,
    __in PCSRT_RESOURCE_GROUP_HEADER ResourceGroup
);
ULONG __declspec(dllimport) NtBuildNumber;

//
// ------------------------------------------------------------------ Functions
//

NTSTATUS GitInitialize(
    __in PVOID TimerDataPtr
)
/*++

Routine Description:

    This routine initializes the Git timer hardware.

Arguments:

    TimerDataPtrPtr - Supplies a pointer to the timer's internal data.

Return Value:

    Status code.

--*/
{
    PGIT_INTERNAL_DATA TimerPtr = (PGIT_INTERNAL_DATA)TimerDataPtr;
    // CNTKCTL = CNTKCTL_PL0VCTEN
    _MoveToCoprocessor(2u, 15, 0, 14, 1, 0);
    if (TimerPtr->Type == GitTimerInterrupts)
        _MoveToCoprocessor(0, 15, 0, 14, 3, 1);
    // Frequency = CNTFRQ
    ULONG Frequency = _MoveFromCoprocessor(15, 0, 14, 0, 0);
    if (Frequency != g_GitFrequency)
        return STATUS_DEVICE_NOT_READY;
    return STATUS_SUCCESS;
}

ULONGLONG GitQueryCounter(
    __in_opt PVOID TimerDataPtr
)
/*++

Routine Description:

    This routine queries the Git timer hardware and retrieves the current
    counter value.

Arguments:

    TimerDataPtr - Supplies a pointer to the timer's internal data.

Return Value:

    Returns the hardware's current count.

--*/
{
    UNREFERENCED_PARAMETER(TimerDataPtr);
    // return CNTVCT
    return _MoveFromCoprocessor64(15, 1, 14);
}

VOID GitSetTimerCompareValue(
    __in ULONGLONG CompareValue
)
/*++

Routine Description:

    This routine sets the current counter value of the Git timer hardware.

Arguments:

    CompareValue - The counter value to set.

Return Value:

    None.

--*/
{
    // CNTV_CVAL = Value
    _MoveToCoprocessor64(CompareValue, 15, 3, 14);
    return;
}

NTSTATUS GitArmTimer(
    __in PVOID TimerDataPtr, 
    __in TIMER_MODE Mode, 
    __in ULONGLONG TickCount
)
/*++

Routine Description:

    This routine arms an Git timer to fire an interrupt after a
    given period of time.

Arguments:

    TimerDataPtr - Supplies a pointer to the timer's internal data.

    Mode - Supplies the desired mode to arm the timer with (pseudo-periodic or
        one-shot).

    TickCount - Supplies the number of ticks from now that the timer should
        interrupt in.

Return Value:

    STATUS_SUCCESS on success.

    STATUS_INVALID_PARAMETER if too large a tick count or an invalid mode was
        supplied.

    STATUS_UNSUCCESSFUL if the hardware could not be programmed.

--*/
{
    PGIT_INTERNAL_DATA TimerPtr = (PGIT_INTERNAL_DATA)TimerDataPtr;
    TimerPtr->PeriodicInterval = TickCount;
    ULONGLONG CompareValue = GitQueryCounter(0) + TickCount;
    TimerPtr->LastCompareValue = CompareValue;
    TimerPtr->Mode = Mode;
    GitSetTimerCompareValue(CompareValue);
    // CNTV_CTL = ENABLE;
    _MoveToCoprocessor(1u, 15, 0, 14, 3, 1);
    return STATUS_SUCCESS;
}
VOID GitAcknowledgeInterrupt(
    __in PVOID TimerDataPtr
)
/*++

Routine Description:

    This routine acknowledges a Git timer interrupt.

Arguments:

    TimerDataPtr - Supplies a pointer to the timer's internal data.

Return Value:

    None.

--*/
{
    PGIT_INTERNAL_DATA TimerPtr = (PGIT_INTERNAL_DATA)TimerDataPtr;
    if (TimerPtr->Mode == TimerModePseudoPeriodic)
    {
        ULONGLONG Counter = GitQueryCounter(0);
        ULONGLONG PeriodicInterval = TimerPtr->PeriodicInterval;
        ULONGLONG LastCompareValue = TimerPtr->LastCompareValue;

        if (((10 * PeriodicInterval) + LastCompareValue) >= Counter)
        {
            LastCompareValue += PeriodicInterval;
        }
        else
        {
            LastCompareValue = Counter + PeriodicInterval;
        }

        TimerPtr->LastCompareValue = LastCompareValue;
        GitSetTimerCompareValue(LastCompareValue);
    }
    return;
}


VOID GitStop(
    __in PVOID TimerDataPtr
)
/*++

Routine Description:

    This routine stops the Git timer from generating interrupts.

Arguments:

    TimerDataPtr - Supplies a pointer to the timer's internal data.

Return Value:

    None.

--*/
{
    PGIT_INTERNAL_DATA TimerPtr = (PGIT_INTERNAL_DATA)TimerDataPtr;
    // CNTV_CTL = 0;
    _MoveToCoprocessor(0, 15, 0, 14, 3, 1);
    TimerPtr->Mode = TimerModeInvalid;
}

NTSTATUS GitDiscover(
    __in ULONG Handle,
    __in PCSRT_RESOURCE_GROUP_HEADER ResourceGroup
)
/*++

Routine Description:

    This function registers the ARM Git timer with the HAL.

Arguments:

    Handle - Supplies the HAL Extension handle which must be passed to other
        HAL Extension APIs.

    ResourceGroup - Supplies a pointer to the Resource Group which the
        HAL Extension has been installed on.

Return Value:

    NTSTATUS code.

--*/
{
    NTSTATUS result = STATUS_SUCCESS;
    //UNICODE_STRING resourceUnicodeId;
    TIMER_INITIALIZATION_BLOCK NewTimer = { 0 };
    GIT_INTERNAL_DATA internalData = { 0 };
    CSRT_RESOURCE_DESCRIPTOR_HEADER ResourceDescriptorHeader = { 0 };
    KINTERRUPT_MODE TimerInterruptMode = LevelSensitive;
    ULONG TimerEventGsiv = 0;
    BOOLEAN TimerIsActiveHigh = FALSE;

    g_WindowsBuildNumber = NtBuildNumber;
    g_WindowsBuildNumber &= 0xffff;
    //
    // DEV HACK: Makeup a resource type until we get correct CSRT parsing.
    //

    ResourceDescriptorHeader.Type = CSRT_RD_TYPE_TIMER;
    ResourceDescriptorHeader.Subtype = CSRT_RD_SUBTYPE_TIMER;
    ResourceDescriptorHeader.Length = sizeof(CSRT_RESOURCE_DESCRIPTOR_HEADER);
    ResourceDescriptorHeader.Uid = 0;

    // Try the GTDT table
    PGTDT_TABLE Gtdt = GetAcpiTable(Handle, 'TDTG', 0, 0);
    if (Gtdt != NULL) {
        TimerEventGsiv = Gtdt->VirtualTimerEventGsiv;
        TimerIsActiveHigh = (Gtdt->VirtualTimerEventFlags & 2) != 0;
        if ((Gtdt->VirtualTimerEventFlags & 1) != 0) TimerInterruptMode = Latched;
    }
    else {
        // GTDT table not found, try the EGIT table
        PEGIT_TABLE Egit = GetAcpiTable(Handle, 'TIGE', 0, 0);
        if (Egit == NULL) return STATUS_UNSUCCESSFUL;
        TimerEventGsiv = Egit->VirtualTimerEventGsiv;
        TimerIsActiveHigh = (Egit->VirtualTimerEventFlags & 2) == 0;
        if ((Egit->VirtualTimerEventFlags & 1) != 0) TimerInterruptMode = Latched;
    }
    g_GitFrequency = _MoveFromCoprocessor(15, 0, 14, 0, 0);
    if (!g_GitFrequency)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    //
    // Initialize the timer registration structure.
    //

    RtlZeroMemory(&NewTimer, sizeof(NewTimer));
    RtlZeroMemory(&internalData, sizeof(internalData));
    INITIALIZE_TIMER_HEADER(&NewTimer);
    NewTimer.Capabilities = TIMER_PER_PROCESSOR | TIMER_COUNTER_READABLE;
    if (g_WindowsBuildNumber >= 8000u) {
        NewTimer.Capabilities |= TIMER_ALWAYS_ON; // 8061 sets TIMER_ALWAYS_ON here, 7915 doesn't implement that flag
    }
    NewTimer.CounterBitWidth = 64;
    NewTimer.FunctionTable.Initialize = (PTIMER_INITIALIZE)GitInitialize;
    NewTimer.CounterFrequency = g_GitFrequency;
    NewTimer.FunctionTable.QueryCounter = (PTIMER_QUERY_COUNTER)GitQueryCounter;
    NewTimer.InternalData = &internalData;
    NewTimer.InternalDataSize = sizeof(internalData);
    NewTimer.KnownType = TimerUnknown;
    if (g_WindowsBuildNumber >= 8000u) {
        NewTimer.KnownType = TimerGit; // TimerGit used in 8061. obviously earlier builds don't have it!
    }
    NewTimer.MaxDivisor = 1;
    NewTimer.Identifier = 0;  
    internalData.Type = GitTimerCounter;
    //RtlInitUnicodeString(&resourceUnicodeId, L"VEN_vvvv&DEV_dddd&SUBVEN_ssss&SUBDEV_yyyy&REV_rrrr&INST_iiii&UID_uuuuuuuu");
    //result = TimerRegister(&NewTimer, &unicodeString);
    result = RegisterResourceDescriptor(Handle, ResourceGroup, &ResourceDescriptorHeader, &NewTimer);
    if (result >= 0)
    {
        ResourceDescriptorHeader.Uid = 1;
        NewTimer.Interrupt.Gsi = TimerEventGsiv;
        NewTimer.Interrupt.Polarity = InterruptActiveHigh;
        NewTimer.Interrupt.Polarity = TimerIsActiveHigh ? InterruptActiveHigh : InterruptActiveLow;
        NewTimer.Interrupt.Mode = TimerInterruptMode;
        NewTimer.Capabilities = TIMER_PER_PROCESSOR | TIMER_PSEUDO_PERIODIC_CAPABLE | TIMER_ONE_SHOT_CAPABLE | TIMER_GENERATES_LINE_BASED_INTERRUPTS;
        if (g_WindowsBuildNumber >= 8000u) {
            NewTimer.Capabilities |= TIMER_ALWAYS_ON; // 8061 sets TIMER_ALWAYS_ON here, 7915 doesn't implement that flag
        }
        NewTimer.FunctionTable.QueryCounter = NULL;
        NewTimer.FunctionTable.ArmTimer = (PTIMER_ARM_TIMER)GitArmTimer;
        NewTimer.FunctionTable.AcknowledgeInterrupt = (PTIMER_ACKNOWLEDGE_INTERRUPT)GitAcknowledgeInterrupt;
        NewTimer.FunctionTable.Stop = (PTIMER_STOP)GitStop;
        NewTimer.Identifier = 1;    
        internalData.Type = GitTimerInterrupts;
        //return TimerRegister(&NewTimer, &resourceUnicodeId);
        return RegisterResourceDescriptor(Handle, ResourceGroup, &ResourceDescriptorHeader, &NewTimer);
    }
    return result;
}

NTSTATUS
AddResourceGroup(
    __in ULONG Handle,
    __in PCSRT_RESOURCE_GROUP_HEADER ResourceGroup
)

/*++

Routine Description:

    This function registers the ARM extensions with the HAL.

Arguments:

    Handle - Supplies the HAL Extension handle which must be passed to other
        HAL Extension APIs.

    ResourceGroup - Supplies a pointer to the Resource Group which the
        HAL Extension has been installed on.

Return Value:

    NTSTATUS code.

--*/

{
    NTSTATUS Status;

    //
    // Register the main timer block.
    //

    Status = GitDiscover(Handle, ResourceGroup);
    if (!NT_SUCCESS(Status)) {
        goto AddResourceGroupEnd;
    }

    Status = STATUS_SUCCESS;

AddResourceGroupEnd:
    return Status;
}
