#include "intro.h"
#include "vmguest.h"
#include "data.h"
#include "vmx.h"
#include "mzpe.h"

STATUS
IntFindKernelBase(
    _In_ QWORD KernelAddr
)
{
    STATUS status = STATUS_SUCCESS;
    BOOLEAN found = FALSE;
    DWORD nrOfPages = 0;
    QWORD kernelAddr = KernelAddr;
    WORD readWord = 0;

    if (0 == KernelAddr)
    {
        return STATUS_UNSUCCESSFUL;
    }

    kernelAddr &= PAGE_MASK;
    while (!found && nrOfPages < 100)
    {
        status = GuestReadWord(kernelAddr, &readWord);
        if (!SUCCEEDED(status))
        {
            LOGL("GuestReadWord failed with status: 0x%x\n", status);
            return STATUS_UNSUCCESSFUL;
        }

        if ('ZM' == readWord)
        {
            LOGL("Kernel Base found at: 0x%x\n", kernelAddr);
            gGlobalData.Intro.KernelBase = kernelAddr;
            found = TRUE;
        }

        if (found)
        {
            return STATUS_SUCCESS;
        }

        kernelAddr -= PAGE_SIZE;
    }

    return STATUS_UNSUCCESSFUL;
}

STATUS
IntGetActiveProcessesList(
    _In_ DWORD BufferSize,
    _Out_ CHAR* const Buffer,
    _Out_opt_ DWORD* const NrOfProcesses)
{
    STATUS status = STATUS_SUCCESS;
    QWORD initialSystemProcessSymbol = 0;
    DWORD initialSystemEprocess = 0;
    PVOID hostVa = NULL;
    PVOID hostPa = NULL;
    DWORD processPid = 0;
    BYTE processName[16] = { 0 };
    DWORD eprocessAddr = 0;
    DWORD copiedSize = 0;
    DWORD listEntryFlink = 0;
    CHAR tempBuffer[255];

    if (0 == gGlobalData.Intro.KernelBase)
    {
        LOGL("Intro not initialized\n");
        return STATUS_UNSUCCESSFUL;
    }

    if (NULL == Buffer)
    {
        LOGL("Buffer is NULL\n");
        return STATUS_INVALID_PARAMETER2;
    }

    status = MzpeFindExport(gGlobalData.Intro.KernelBase, "PsInitialSystemProcess", &initialSystemProcessSymbol);
    if (!SUCCEEDED(status))
    {
        LOGL("MzpeFindExport failed with status: 0x%x\n", status);
        return STATUS_UNSUCCESSFUL;
    }

    status = GuestReadDword(initialSystemProcessSymbol, &initialSystemEprocess);
    if (!SUCCEEDED(status))
    {
        LOGL("GuestReadDword failed with status: 0x%x\n", status);
        return STATUS_UNSUCCESSFUL;
    }

    LOGL("Initial System Eprocess: 0x%x\n", initialSystemEprocess);

    eprocessAddr = initialSystemEprocess;

    _get_next_process:

    status = GuestVAToHostVA(eprocessAddr, &hostPa, &hostVa);
    if (!SUCCEEDED(status))
    {
        LOGL("GuestVAToHostVA failed with status: 0x%x\n", status);
        return STATUS_UNSUCCESSFUL;
    }
    
    processPid = *(PDWORD)((BYTE*)hostVa + 0xb4);
    memcpy(processName, (BYTE*)hostVa + 0x17c, 15);
    processName[15] = '\0';
    listEntryFlink = (*(PDWORD)((BYTE*)hostVa + 0xb8));
    eprocessAddr = listEntryFlink - 0xb8;

    if (0 == processPid)
    {
        goto _skip;
    }

    if (NULL != NrOfProcesses)
    {
        (*NrOfProcesses)++;
    }

    sprintf(tempBuffer, "%s;%d;", processName, processPid);  
    memcpy(Buffer + copiedSize, tempBuffer, strlen(tempBuffer));
    copiedSize += strlen(tempBuffer);
    *(Buffer + copiedSize) = '\0';

    sprintf(tempBuffer, "%s PID: %d", processName, processPid);
    LOGL("%s\n", tempBuffer);

_skip:
    status = UnmapMemory(hostPa, PAGE_SIZE);
    if (!SUCCEEDED(status))
    {
        LOGL("UnmapMemory failed with status: 0x%x\n", status);
    }

    if ((copiedSize < BufferSize) &&
        (eprocessAddr != initialSystemEprocess))
    {
        goto _get_next_process;
    }

    return STATUS_SUCCESS;
}