// HAL Extension API.

#include "minhalext.h"

static void GetSecurityCookie(
	__out PULONG *pSecurityCookie,
	__out PULONG *pSecurityCookieComplement
) {
	static ULONG cookie, complement;
	*pSecurityCookie = &cookie;
	*pSecurityCookieComplement = &complement;
}

static OS_EXTENSION_HAL_EXTENSION_EXPORTS s_Exports = {
	AddResourceGroup,
	GetSecurityCookie
};

static POS_EXTENSION_HAL_EXTENSION_IMPORTS s_Imports = NULL;

NTSTATUS HalExtensionInit(
	__in OS_EXTENSION_INTERFACE Interface,
	__out POS_EXTENSION_HAL_EXTENSION_EXPORTS* Exports,
	__in POS_EXTENSION_HAL_EXTENSION_IMPORTS Imports
) {
	if (Interface != OsExtensionHalExtension) return STATUS_NOINTERFACE;
	*Exports = &s_Exports;
	s_Imports = Imports;
	return STATUS_SUCCESS;
}

NTSTATUS
RegisterResourceDescriptor (
    __in ULONG Handle,
    __in PCSRT_RESOURCE_GROUP_HEADER ResourceGroup,
    __in PCSRT_RESOURCE_DESCRIPTOR_HEADER ResourceDescriptor,
    __in_opt PVOID ResourceDescriptorInfo
    ) {
	return s_Imports->RegisterResourceDescriptor(Handle, ResourceGroup, ResourceDescriptor, ResourceDescriptorInfo);
}

PCSRT_RESOURCE_DESCRIPTOR_HEADER
GetNextResourceDescriptor(
	__in ULONG Handle,
	__in PCSRT_RESOURCE_GROUP_HEADER ResourceGroup,
	__in_opt PCSRT_RESOURCE_DESCRIPTOR_HEADER ResourceDescriptor,
	__in USHORT ResourceType,
	__in USHORT ResourceSubtype,
	__in ULONG ResourceID
) {
	return s_Imports->GetNextResourceDescriptor(
		Handle,
		ResourceGroup,
		ResourceDescriptor,
		ResourceType,
		ResourceSubtype,
		ResourceID
	);
}

PVOID
GetAcpiTable (
    __in ULONG Handle,
    __in ULONG Signature,
    __in_opt PCSTR OemId,
    __in_opt PCSTR OemTableId
    ) {
	return s_Imports->GetAcpiTable(
		Handle,
		Signature,
		OemId,
		OemTableId
	);
}