// HAL Extension header
#pragma once

typedef void* PINTERRUPT_END_OF_INTERRUPT;

#include "nthalext.h"

// Interface
typedef enum _OS_EXTENSION_INTERFACE {
	OsExtensionHalExtension = 0
} OS_EXTENSION_INTERFACE;

// Functions
typedef NTSTATUS
(*tfpAddResourceGroup)(
    __in ULONG Handle,
    __in PCSRT_RESOURCE_GROUP_HEADER ResourceGroup
    );

typedef void
(*tfpGetSecurityCookie)(
	__out PULONG *pSecurityCookie,
	__out PULONG *pSecurityCookieComplement
);

#define HALEXT_API(name) tfp##name name

typedef struct _OS_EXTENSION_HAL_EXTENSION_EXPORTS
{
  HALEXT_API(AddResourceGroup);
  HALEXT_API(GetSecurityCookie);
} OS_EXTENSION_HAL_EXTENSION_EXPORTS, *POS_EXTENSION_HAL_EXTENSION_EXPORTS;

typedef NTSTATUS
(*tfpRegisterResourceDescriptor) (
    __in ULONG Handle,
    __in PCSRT_RESOURCE_GROUP_HEADER ResourceGroup,
    __in_opt PCSRT_RESOURCE_DESCRIPTOR_HEADER ResourceDescriptor,
    __in_opt PVOID ResourceDescriptorInfo
    );

typedef PCSRT_RESOURCE_DESCRIPTOR_HEADER
(*tfpGetNextResourceDescriptor) (
    __in ULONG Handle,
    __in PCSRT_RESOURCE_GROUP_HEADER ResourceGroup,
    __in_opt PCSRT_RESOURCE_DESCRIPTOR_HEADER ResourceDescriptor,
    __in USHORT ResourceType,
    __in USHORT ResourceSubtype,
    __in ULONG ResourceID
    );

typedef PVOID
(*tfpGetAcpiTable) (
    __in ULONG Handle,
    __in ULONG Signature,
    __in_opt PCSTR OemId,
    __in_opt PCSTR OemTableId
    );

typedef struct _OS_EXTENSION_HAL_EXTENSION_IMPORTS {
	HALEXT_API(RegisterResourceDescriptor);
	HALEXT_API(GetNextResourceDescriptor);
	HALEXT_API(GetAcpiTable);
} OS_EXTENSION_HAL_EXTENSION_IMPORTS, *POS_EXTENSION_HAL_EXTENSION_IMPORTS;

NTSTATUS HalExtensionInit(
	__in OS_EXTENSION_INTERFACE Interface,
	__out POS_EXTENSION_HAL_EXTENSION_EXPORTS* Exports,
	__in POS_EXTENSION_HAL_EXTENSION_IMPORTS Imports
);
