/*
#  Copyright (c) 2016-2017, Linaro Limited. All rights reserved.
#
#  This program and the accompanying materials
#  are licensed and made available under the terms and conditions of the BSD License
#  which accompanies this distribution.  The full text of the license may be found at
#  http://opensource.org/licenses/bsd-license.php
#
#  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
#  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
#
*/
#include <RdkBootManagerLib.h>

#define RDK_LINUX_KERNEL_NAME   L"VenHw(B549F005-4BD4-4020-A0CB-06F42BDA68C3)/HD(6,GPT,5C0F213C-17E1-4149-88C8-8B50FB4EC70E,0x7000,0x20000)/EFI/BOOT/Image"
#define RDK_SERVER_NAME         L"VenHw(B549F005-4BD4-4020-A0CB-06F42BDA68C3)/HD(6,GPT,5C0F213C-17E1-4149-88C8-8B50FB4EC70E,0x7000,0x20000)/EFI/BOOT/Server.url"
#define RDK_FDT_NAME            L"VenHw(B549F005-4BD4-4020-A0CB-06F42BDA68C3)/HD(6,GPT,5C0F213C-17E1-4149-88C8-8B50FB4EC70E,0x7000,0x20000)/EFI/BOOT/hi6220-hikey.dtb"

#define LABEL_SYSTEM_PARTITION  "system"

STATIC EFI_LOAD_FILE_PROTOCOL  *LoadFile = NULL;
STATIC HTTP_BOOT_PRIVATE_DATA  *Private  = NULL;

STATIC
VOID
HttpPrivateFromLoadFile (
  IN   EFI_LOAD_FILE_PROTOCOL   *LoadFile,
  OUT  HTTP_BOOT_PRIVATE_DATA   **Private
  )
{
  HTTP_BOOT_VIRTUAL_NIC  *Ip4Nic = NULL;

  INT64 Offset = (INT64)&Ip4Nic->LoadFile;
  Ip4Nic = (VOID *)((char *)LoadFile - Offset);
  ASSERT (Ip4Nic->Signature == HTTP_BOOT_VIRTUAL_NIC_SIGNATURE);
  *Private = Ip4Nic->Private;
}

STATIC
VOID
HttpGetLoadFileHandle (
  OUT EFI_LOAD_FILE_PROTOCOL  **LoadFile
  )
{
  EFI_STATUS                        Status;
  UINTN                             LoopIndex;
  UINTN                             NumHandles;
  EFI_HANDLE                        *AllHandles;
  EFI_HANDLE                        Handle;
  EFI_DEVICE_PATH_PROTOCOL          *DevicePath;
  EFI_DEVICE_PATH_TO_TEXT_PROTOCOL  *DevPathToText;
  UINT16                            *DeviceFullPath;

  Status = gBS->LocateProtocol (
      &gEfiDevicePathToTextProtocolGuid,
      NULL,
      (VOID **) &DevPathToText
      );
  ASSERT_EFI_ERROR (Status);

  // Get every LoadFile protocol instance installed in the system
  Status = gBS->LocateHandleBuffer (
      ByProtocol,
      &gEfiLoadFileProtocolGuid,
      NULL,
      &NumHandles,
      &AllHandles
      );
  ASSERT_EFI_ERROR (Status);

  // Get HTTP driver handle from AllHandles
  for (LoopIndex = 0; LoopIndex < NumHandles; LoopIndex++) {

    Handle = AllHandles[LoopIndex];

    // Get the device path for the handle
    Status = gBS->OpenProtocol (
        Handle,
        &gEfiDevicePathProtocolGuid,
        (VOID **) &DevicePath,
        gImageHandle,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL
        );
    ASSERT_EFI_ERROR (Status);

    DeviceFullPath = DevPathToText->ConvertDevicePathToText (
        DevicePath,                     
        FALSE,     
        TRUE  
        );

    ASSERT(DeviceFullPath != NULL); 

    if(StrStr(DeviceFullPath, L"IPv4") != NULL) {

      Status = gBS->OpenProtocol (
          Handle,
          &gEfiLoadFileProtocolGuid,
          (VOID **) LoadFile,
          gImageHandle,
          NULL,
          EFI_OPEN_PROTOCOL_GET_PROTOCOL
          );
      ASSERT_EFI_ERROR (Status);

      FreePool (AllHandles);
      break;
    }
  }

  ASSERT ( LoopIndex < NumHandles );
}

STATIC
EFI_STATUS
HttpUpdatePath (
  IN   CHAR16                   *Uri,
  OUT  EFI_DEVICE_PATH_PROTOCOL **NewDevicePath
  )
{
  EFI_DEV_PATH              *Node;
  EFI_DEVICE_PATH_PROTOCOL  *TmpDevicePath;
  EFI_STATUS                Status;
  UINTN                     Index;
  UINTN                     Length;
  CHAR8                     AsciiUri[URI_STR_MAX_SIZE];

  Node           = NULL;
  TmpDevicePath  = NULL;
  Status         = EFI_SUCCESS;

  // Convert the scheme to all lower case.
  for (Index = 0; Index < StrLen (Uri); Index++) {
    if (Uri[Index] == L':') {
      break;
    }
    if (Uri[Index] >= L'A' && Uri[Index] <= L'Z') {
      Uri[Index] -= (CHAR16)(L'A' - L'a');
    }
  }

  // Only accept empty URI, or http and https URI.
  if ((StrLen (Uri) != 0) && (StrnCmp (Uri, L"http://", 7) != 0) && (StrnCmp (Uri, L"https://", 8) != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  // Create a new device path by appending the IP node and URI node to
  // the driver's parent device path
  Node = AllocateZeroPool (sizeof (IPv4_DEVICE_PATH));
  if (Node == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }
  Node->Ipv4.Header.Type    = MESSAGING_DEVICE_PATH;
  Node->Ipv4.Header.SubType = MSG_IPv4_DP;
  SetDevicePathNodeLength (Node, sizeof (IPv4_DEVICE_PATH));
  TmpDevicePath = AppendDevicePathNode (Private->ParentDevicePath, (EFI_DEVICE_PATH_PROTOCOL*) Node);
  FreePool (Node);
  if (TmpDevicePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  
  // Update the URI node with the input boot file URI.
  UnicodeStrToAsciiStrS (Uri, AsciiUri, sizeof (AsciiUri));
  Length = sizeof (EFI_DEVICE_PATH_PROTOCOL) + AsciiStrSize (AsciiUri);
  Node = AllocatePool (Length);
  if (Node == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    FreePool (TmpDevicePath);
    goto ON_EXIT;
  }
  Node->DevPath.Type    = MESSAGING_DEVICE_PATH;
  Node->DevPath.SubType = MSG_URI_DP;
  SetDevicePathNodeLength (Node, Length);
  CopyMem ((UINT8*) Node + sizeof (EFI_DEVICE_PATH_PROTOCOL), AsciiUri, AsciiStrSize (AsciiUri));
  *NewDevicePath = AppendDevicePathNode (TmpDevicePath, (EFI_DEVICE_PATH_PROTOCOL*) Node);
  FreePool (Node);
  FreePool (TmpDevicePath);
  if (*NewDevicePath == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

ON_EXIT:

  return Status;
}

STATIC
EFI_STATUS
HttpGetImage (
  IN   CHAR16  *Uri,
  OUT  UINT8   **FileBuffer,
  OUT  UINTN   *FileSize
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *NewDevicePath;
  EFI_STATUS                Status;

  *FileBuffer   = NULL;
  NewDevicePath = NULL;
  *FileSize     = 0;

  // Get the LoadFile Handle and 
  // Private structure of HTTP driver
  if (LoadFile == NULL) {
    HttpGetLoadFileHandle (&LoadFile);
    HttpPrivateFromLoadFile (LoadFile, &Private);
  }

  // Update URI path
  Status = HttpUpdatePath (Uri, &NewDevicePath);  
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  // Get the HTTP image from server
  Status = LoadFile->LoadFile (LoadFile, NewDevicePath, TRUE, FileSize, *FileBuffer);
  if((Status != EFI_WARN_FILE_SYSTEM) && (Status != EFI_BUFFER_TOO_SMALL)) {
    goto ON_EXIT;
  }

  *FileBuffer = AllocatePool (*FileSize);
  if (*FileBuffer == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  Status = LoadFile->LoadFile (LoadFile, NewDevicePath, TRUE, FileSize, *FileBuffer);
  if (EFI_ERROR (Status)) {
    FreePool (FileBuffer);
    goto ON_EXIT;
  }

ON_EXIT:

  if (NewDevicePath != NULL) {
    FreePool (NewDevicePath);
  }

  return Status;
}

EFI_STATUS
RdkHttpBoot (
  VOID
  )
{
  EFI_STATUS  Status;
  VOID        *FilePtr;
  UINT8       *FileBuffer;
  UINT16      *Uri;  
  UINTN       FileSize;
  UINTN       LoopIndex;
  UINTN       Size;

  // Get the Server name stored in file Server.url
  Status = RdkReadFile(RDK_SERVER_NAME, (VOID **)&FileBuffer, &FileSize);
  ASSERT_EFI_ERROR (Status);

  Uri = AllocateZeroPool (sizeof(*Uri) * (FileSize+1));
  if (Uri == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    ASSERT_EFI_ERROR (Status);
  }

  for(LoopIndex=0; LoopIndex<FileSize; LoopIndex++) {
    Uri[LoopIndex] = FileBuffer[LoopIndex];
  }

  if(FileBuffer[FileSize-1] == '\n') {
    Uri[FileSize-1] = '\0';
  }

  FreePool (FileBuffer);
  DEBUG ((EFI_D_ERROR, "%s", Uri));
  
  // Disable watchdog
  Status = gBS->SetWatchdogTimer (0, 0x10000, 0, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "HttpBoot: Couldn't disable watchdog timer: %r\n", Status));
  }
  
  // Get the File from server using it's URI
  Status = HttpGetImage (Uri, &FileBuffer, &FileSize); 
  ASSERT_EFI_ERROR (Status);

  // Write the received image to flash
  FilePtr   = FileBuffer;
  Size      = Str2Int(FilePtr);
  FilePtr  += FILE_HDR_SIZE;
  Status    = PartitionWrite(LABEL_SYSTEM_PARTITION, FilePtr, Size);
  ASSERT_EFI_ERROR (Status);

  FilePtr  += Size;
  Size      = Str2Int(FilePtr);
  FilePtr  += FILE_HDR_SIZE;
  Status    = RdkWriteFile(RDK_LINUX_KERNEL_NAME, &FilePtr, &Size);
  ASSERT_EFI_ERROR (Status);

  FilePtr  += Size;
  Size      = Str2Int(FilePtr);
  FilePtr  += FILE_HDR_SIZE;
  Status    = RdkWriteFile(RDK_FDT_NAME, &FilePtr, &Size);
  ASSERT_EFI_ERROR (Status);

  FreePool (FileBuffer);
  FreePool (Uri);

  return Status;
}
