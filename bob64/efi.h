#ifndef BOB64_EFI_H
#define BOB64_EFI_H

#include "types.h"

#define EFIAPI __attribute__((ms_abi))
typedef u16 CHAR16;
typedef uintptr_t UINTN;
typedef u64 EFI_STATUS;
typedef void *EFI_HANDLE;

#define EFI_SUCCESS 0ULL
#define EFI_INVALID_PARAMETER 0x8000000000000002ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define EFI_UNSUPPORTED 0x8000000000000003ULL
#define EFI_ERROR(status) (((EFI_STATUS)(status) >> 63) != 0)
#define EFI_MEMORY_CONVENTIONAL 7u
#define EFI_MEMORY_LOADER_DATA 2u
#define EFI_PAGE_SIZE 4096ULL

typedef struct {
    u64 Signature;
    u32 Revision;
    u32 HeaderSize;
    u32 CRC32;
    u32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    u32 Type;
    u32 Pad;
    u64 PhysicalStart;
    u64 VirtualStart;
    u64 NumberOfPages;
    u64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *,EFI_MEMORY_DESCRIPTOR *,
                                                UINTN *,UINTN *,u32 *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(u32,UINTN,void **);
typedef struct {
    u32 Data1;
    u16 Data2;
    u16 Data3;
    u8 Data4[8];
} EFI_GUID;
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE,const EFI_GUID *,void **);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE,UINTN);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(const EFI_GUID *,void *,void **);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    void *AllocatePages;
    void *FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    void *FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *GetNextMonotonicCount;
    void *Stall;
    void *SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
} EFI_BOOT_SERVICES;

typedef struct {
    u32 Version;
    u32 HorizontalResolution;
    u32 VerticalResolution;
    u32 PixelFormat;
    u32 PixelInformation[4];
    u32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    u32 MaxMode;
    u32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    u64 FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, u8);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_OUTPUT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, const CHAR16 *);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET Reset;
    EFI_TEXT_OUTPUT_STRING OutputString;
    void *TestString;
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    void *ClearScreen;
    void *SetCursorPosition;
    void *EnableCursor;
    void *Mode;
};

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    u32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    u32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    u32 LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    u64 ImageSize;
    u32 ImageCodeType;
    u32 ImageDataType;
    void *UnloadImage;
} EFI_LOADED_IMAGE_PROTOCOL;

_Static_assert(sizeof(uintptr_t)==sizeof(EFI_STATUS),"EFI x64 status width mismatch");
_Static_assert(__builtin_offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL,OutputString)==8,
               "UEFI text protocol layout mismatch");
_Static_assert(__builtin_offsetof(EFI_SYSTEM_TABLE,ConOut)==64,
               "UEFI x64 system table layout mismatch");
_Static_assert(sizeof(EFI_SYSTEM_TABLE)==120,"UEFI x64 system table size mismatch");
_Static_assert(sizeof(EFI_MEMORY_DESCRIPTOR)==40,"UEFI x64 memory descriptor layout mismatch");
_Static_assert(__builtin_offsetof(EFI_BOOT_SERVICES,GetMemoryMap)==56,
               "UEFI x64 boot services layout mismatch");
_Static_assert(__builtin_offsetof(EFI_BOOT_SERVICES,AllocatePool)==64,
               "UEFI x64 allocation service layout mismatch");
_Static_assert(__builtin_offsetof(EFI_BOOT_SERVICES,HandleProtocol)==152,
               "UEFI x64 HandleProtocol table offset mismatch");
_Static_assert(__builtin_offsetof(EFI_BOOT_SERVICES,ExitBootServices)==232,
               "UEFI x64 ExitBootServices table offset mismatch");
_Static_assert(__builtin_offsetof(EFI_BOOT_SERVICES,LocateProtocol)==320,
               "UEFI x64 LocateProtocol table offset mismatch");
_Static_assert(sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION)==36,
               "UEFI GOP mode information layout mismatch");
_Static_assert(__builtin_offsetof(EFI_GRAPHICS_OUTPUT_PROTOCOL,Mode)==24,
               "UEFI GOP protocol mode pointer offset mismatch");
_Static_assert(__builtin_offsetof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE,FrameBufferBase)==24,
               "UEFI GOP framebuffer base offset mismatch");
_Static_assert(__builtin_offsetof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE,FrameBufferSize)==32&&
               sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE)==40,
               "UEFI GOP mode layout mismatch");
_Static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL,ImageBase)==64,
               "UEFI loaded image base offset mismatch");
_Static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL,ImageSize)==72,
               "UEFI loaded image size offset mismatch");
_Static_assert(sizeof(EFI_LOADED_IMAGE_PROTOCOL)==96,
               "UEFI x64 loaded image protocol size mismatch");

#endif
