/*
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <app/clusters/ota-requestor/OTADownloader.h>
#include <app/clusters/ota-requestor/OTARequestorInterface.h>

#include "OTAImageProcessorImpl.h"
extern "C" {
#if CHIP_DEVICE_LAYER_TARGET_BL616
#include <bflb_ota.h>
#include <bflb_efuse.h>
#else
#include <hosal_ota.h>
#include <hosal_efuse.h>
#endif

void hal_reboot (void);
}

#define OTA_HEADER_HASH_SIZE 32
static constexpr const uint8_t kOtaImage_xz[] = {0xfd, 0x37, 0x7a, 0x58, 0x5a};
static constexpr const uint8_t kOtaImage_otaHdr[] = {0x42, 0x4c, 0x36, 0x30, 0x58, 0x5f, 0x4f, 0x54, 0x41, 0x5f, 0x56, 0x65, 0x72, 0x31, 0x2e, 0x30};
static constexpr const uint8_t kOtaImage_typeXz[] = {0x58, 0x5a, 0x20, 0x20};
static constexpr const uint8_t kOtaImage_typeRaw[] = {0x52, 0x41, 0x57, 0x20};

typedef struct ota_header {
    union {
        struct {
            uint8_t header[16];

            uint8_t type[4]; //RAW XZ
            uint32_t len;    //body len
            uint8_t pad0[8];

            uint8_t ver_hardware[16];
            uint8_t ver_software[16];

            uint8_t sha256[OTA_HEADER_HASH_SIZE];
            uint8_t pk_hash[OTA_HEADER_HASH_SIZE];
        } s;
        uint8_t _pad[512];
    } u;
} ota_header_t;

using namespace chip::System;

namespace chip {

bool OTAImageProcessorImpl::IsFirstImageRun()
{
    OTARequestorInterface * requestor = chip::GetRequestorInstance();
    if (requestor == nullptr)
    {
        return false;
    }

    return requestor->GetCurrentUpdateState() == OTARequestorInterface::OTAUpdateStateEnum::kApplying;
}

CHIP_ERROR OTAImageProcessorImpl::ConfirmCurrentImage()
{
    OTARequestorInterface * requestor = chip::GetRequestorInstance();
    if (requestor == nullptr)
    {
        return CHIP_ERROR_INTERNAL;
    }

    uint32_t currentVersion;
    uint32_t targetVersion = requestor->GetTargetVersion();
    ReturnErrorOnFailure(DeviceLayer::ConfigurationMgr().GetSoftwareVersion(currentVersion));
    if (currentVersion != targetVersion)
    {
        ChipLogError(SoftwareUpdate, "Current software version = %" PRIu32 ", expected software version = %" PRIu32, currentVersion,
                     targetVersion);
        return CHIP_ERROR_INCORRECT_STATE;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::PrepareDownload()
{
    DeviceLayer::PlatformMgr().ScheduleWork(HandlePrepareDownload, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::Finalize()
{
    DeviceLayer::PlatformMgr().ScheduleWork(HandleFinalize, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::Apply()
{
    DeviceLayer::PlatformMgr().ScheduleWork(HandleApply, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::Abort()
{
    DeviceLayer::PlatformMgr().ScheduleWork(HandleAbort, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::ProcessBlock(ByteSpan & block)
{
    if ((nullptr == block.data()) || block.empty())
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    // Store block data for HandleProcessBlock to access
    CHIP_ERROR err = SetBlock(block);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "Cannot set block data: %" CHIP_ERROR_FORMAT, err.Format());
    }

    DeviceLayer::PlatformMgr().ScheduleWork(HandleProcessBlock, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

void OTAImageProcessorImpl::OtaAbort(OTAImageProcessorImpl * aImageProcessor, bool isWriteFailure) 
{
    if (isWriteFailure) {
#if CHIP_DEVICE_LAYER_TARGET_BL616
        bflb_ota_abort();
#else
        hosal_ota_abort();
#endif
    }

    if (aImageProcessor->mImageOtaHeader) {
        chip::Platform::MemoryFree(aImageProcessor->mImageOtaHeader);
    }
    aImageProcessor->mImageOtaHeader = nullptr;

    if (isWriteFailure) {
        aImageProcessor->mDownloader->EndDownload(CHIP_ERROR_WRITE_FAILED);
    }
    else {
        aImageProcessor->mDownloader->EndDownload(CHIP_ERROR_OPEN_FAILED);
    }
}

void OTAImageProcessorImpl::HandlePrepareDownload(intptr_t context)
{
    auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);

    if (imageProcessor == nullptr)
    {
        ChipLogError(SoftwareUpdate, "ImageProcessor context is null");
        return;
    }
    else if (imageProcessor->mDownloader == nullptr)
    {
        ChipLogError(SoftwareUpdate, "mDownloader is null");
        return;
    }

    imageProcessor->mParams.downloadedBytes = 0;
    imageProcessor->mParams.totalFileBytes  = 0;
    imageProcessor->mHeaderParser.Init();

    imageProcessor->mDownloader->OnPreparedForDownload(CHIP_NO_ERROR);
}

void OTAImageProcessorImpl::HandleFinalize(intptr_t context)
{
    auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);

    if (imageProcessor == nullptr)
    {
        return;
    }

    if (imageProcessor->mImageOtaHeader) {
        chip::Platform::MemoryFree(imageProcessor->mImageOtaHeader);
    }
    imageProcessor->mImageOtaHeader = nullptr;

#if CHIP_DEVICE_LAYER_TARGET_BL616
    if (bflb_ota_check() < 0)
#else
    if (hosal_ota_check() < 0)
#endif
    {
        imageProcessor->mDownloader->EndDownload(CHIP_ERROR_WRITE_FAILED);
        ChipLogProgress(SoftwareUpdate, "OTA image verification error");
    }
    else
    {
        ChipLogProgress(SoftwareUpdate, "OTA image downloaded");
    }

    imageProcessor->ReleaseBlock();
}

void OTAImageProcessorImpl::HandleApply(intptr_t context)
{
    auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);

    if (imageProcessor == nullptr)
    {
        return;
    }

#if CHIP_DEVICE_LAYER_TARGET_BL616
    bflb_ota_apply();
#else
    hosal_ota_apply(0);
#endif

    DeviceLayer::SystemLayer().StartTimer(
        System::Clock::Seconds32(OTA_AUTO_REBOOT_DELAY),
        [](Layer *, void *) {
            ChipLogProgress(SoftwareUpdate, "Rebooting...");
            vTaskDelay(100);
            hal_reboot();
        },
        nullptr);
}

void OTAImageProcessorImpl::HandleAbort(intptr_t context)
{
    auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);
    if (imageProcessor == nullptr)
    {
        return;
    }

    if (imageProcessor->mImageOtaHeader) {
        chip::Platform::MemoryFree(imageProcessor->mImageOtaHeader);
    }
    imageProcessor->mImageOtaHeader = nullptr;

#if CHIP_DEVICE_LAYER_TARGET_BL616
    bflb_ota_abort();
#else
    hosal_ota_abort();
#endif

    imageProcessor->ReleaseBlock();
}

void OTAImageProcessorImpl::HandleProcessBlock(intptr_t context)
{
    OTAImageHeader header;
    CHIP_ERROR error;
    uint64_t totalSize = 0, offset = 0;
    int writeSize = 0;
    static const uint32_t public_key_hash_empty_efuse [8] = {0,0,0,0,0,0,0,0};
    static const uint32_t public_key_hash_empty_ota_header [8] = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};

    auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);

    if (imageProcessor == nullptr)
    {
        ChipLogError(SoftwareUpdate, "ImageProcessor context is null");
        return;
    }
    else if (imageProcessor->mDownloader == nullptr)
    {
        ChipLogError(SoftwareUpdate, "mDownloader is null");
        return;
    }

    ByteSpan block = imageProcessor->mBlock;
    if (imageProcessor->mHeaderParser.IsInitialized())
    {
        error = imageProcessor->mHeaderParser.AccumulateAndDecode(block, header);
        if (CHIP_ERROR_BUFFER_TOO_SMALL == error)
        {
            return;
        }
        else if (CHIP_NO_ERROR != error)
        {
            ChipLogError(SoftwareUpdate, "Matter image header parser error %s", chip::ErrorStr(error));
            imageProcessor->mDownloader->EndDownload(CHIP_ERROR_INVALID_FILE_IDENTIFIER);
            imageProcessor->mHeaderParser.Clear();
            return;
        }

        ChipLogProgress(SoftwareUpdate, "Image Header software version: %ld payload size: %lu", header.mSoftwareVersion,
                        (long unsigned int) header.mPayloadSize);
        imageProcessor->mParams.totalFileBytes = header.mPayloadSize;
        imageProcessor->mHeaderParser.Clear();

        imageProcessor->mOtaHdrChecked = false;
        imageProcessor->mImageOtaHeader = static_cast<uint8_t *>(chip::Platform::MemoryAlloc(sizeof(ota_header_t)));
        if (nullptr == imageProcessor->mImageOtaHeader) {
            imageProcessor->mDownloader->EndDownload(CHIP_ERROR_NO_MEMORY);
            return;
        }

#if CHIP_DEVICE_LAYER_TARGET_BL616
        if (bflb_ota_start(header.mPayloadSize - sizeof(ota_header_t) + OTA_HEADER_HASH_SIZE) < 0)
#else
        if (hosal_ota_start(header.mPayloadSize - sizeof(ota_header_t) + OTA_HEADER_HASH_SIZE) < 0)
#endif
        {
            imageProcessor->OtaAbort(imageProcessor, false);
            return;
        }
    }

    if (imageProcessor->mParams.totalFileBytes)
    {
        writeSize = block.size();
        if (imageProcessor->mParams.downloadedBytes <= sizeof(ota_header_t)) {

            if (block.size() >= sizeof(ota_header_t) - imageProcessor->mParams.downloadedBytes) {
                writeSize = block.size() - (sizeof(ota_header_t) - imageProcessor->mParams.downloadedBytes);
                memcpy(imageProcessor->mImageOtaHeader + imageProcessor->mParams.downloadedBytes, block.data(), sizeof(ota_header_t) - imageProcessor->mParams.downloadedBytes);
            }
            else {
                writeSize = 0;
                memcpy(imageProcessor->mImageOtaHeader + imageProcessor->mParams.downloadedBytes, block.data(), block.size());
            }
        }

        if (imageProcessor->mParams.downloadedBytes + block.size() >= sizeof(ota_header_t)) {

            if (false == imageProcessor->mOtaHdrChecked) {

                ota_header_t *lOtaHeader = reinterpret_cast<ota_header_t *>(imageProcessor->mImageOtaHeader);
                uint8_t publicKeyHash[OTA_HEADER_HASH_SIZE];

                memset(publicKeyHash, 0, OTA_HEADER_HASH_SIZE);
#if CHIP_DEVICE_LAYER_TARGET_BL616
                bflb_efuse_read_aes_key(0, publicKeyHash, (OTA_HEADER_HASH_SIZE + 3) / 4);
#else
#define EFUSE_PUBLIC_KEY_HASH_OFFSET 0x1C
                hosal_efuse_read(EFUSE_PUBLIC_KEY_HASH_OFFSET, (uint32_t *)publicKeyHash, (OTA_HEADER_HASH_SIZE + 3) / 4);
#endif

                if (memcmp(kOtaImage_otaHdr, imageProcessor->mImageOtaHeader, sizeof(kOtaImage_otaHdr)) || 
                    (memcmp(lOtaHeader->u.s.type, kOtaImage_typeXz, sizeof(kOtaImage_typeXz)) &&
                        memcmp(lOtaHeader->u.s.type, kOtaImage_typeRaw, sizeof(kOtaImage_typeRaw)))) {

                    imageProcessor->OtaAbort(imageProcessor, true);
                    return;
                }

                if (memcmp(public_key_hash_empty_efuse, publicKeyHash, sizeof(publicKeyHash)) || 
                    memcmp(public_key_hash_empty_ota_header, lOtaHeader->u.s.pk_hash, sizeof(lOtaHeader->u.s.pk_hash))) {
                    /** device signed or ota firmware signed or both */

                    if (memcmp(lOtaHeader->u.s.pk_hash, publicKeyHash, sizeof(publicKeyHash))) {
                        /** device signature is not same as ota header */
                        imageProcessor->OtaAbort(imageProcessor, true);
                        return;
                    }
                }

                imageProcessor->mOtaHdrChecked = true;
            }

            if (writeSize > 0) {
                
                totalSize = imageProcessor->mParams.totalFileBytes - sizeof(ota_header_t) + OTA_HEADER_HASH_SIZE;
                offset = imageProcessor->mParams.downloadedBytes ? imageProcessor->mParams.downloadedBytes - sizeof(ota_header_t) : 0;
                // ChipLogProgress(SoftwareUpdate, "ota_update %lld, %lld, %d, %d", offset, totalSize, writeSize, block.size());

#if CHIP_DEVICE_LAYER_TARGET_BL616
                if (bflb_ota_update(totalSize, offset, (uint8_t *) block.data() + block.size() - writeSize, writeSize) < 0)
#else
                if (hosal_ota_update(totalSize, offset, (uint8_t *) block.data() + block.size() - writeSize, writeSize) < 0)
#endif
                {

                    imageProcessor->OtaAbort(imageProcessor, true);
                    return;
                }

                if (imageProcessor->mParams.downloadedBytes + block.size() == imageProcessor->mParams.totalFileBytes) {
                    ota_header_t *lOtaHeader = reinterpret_cast<ota_header_t *>(imageProcessor->mImageOtaHeader);
                    offset += block.size();

#if CHIP_DEVICE_LAYER_TARGET_BL616
                    if (bflb_ota_update(totalSize, offset, lOtaHeader->u.s.sha256, sizeof(lOtaHeader->u.s.sha256)) < 0)
#else
                    if (hosal_ota_update(totalSize, offset, lOtaHeader->u.s.sha256, sizeof(lOtaHeader->u.s.sha256)) < 0)
#endif
                    {
                        imageProcessor->OtaAbort(imageProcessor, true);
                        return;
                    }
                }
            }
        }

        imageProcessor->mParams.downloadedBytes += block.size();
    }

    imageProcessor->mDownloader->FetchNextData();
}

// Store block data for HandleProcessBlock to access
CHIP_ERROR OTAImageProcessorImpl::SetBlock(ByteSpan & block)
{
    if ((block.data() == nullptr) || block.empty())
    {
        return CHIP_NO_ERROR;
    }

    // Allocate memory for block data if we don't have enough already
    if (mBlock.size() < block.size())
    {
        ReleaseBlock();

        mBlock = MutableByteSpan(static_cast<uint8_t *>(chip::Platform::MemoryAlloc(block.size())), block.size());
        if (mBlock.data() == nullptr)
        {
            return CHIP_ERROR_NO_MEMORY;
        }
    }

    // Store the actual block data
    CHIP_ERROR err = CopySpanToMutableSpan(block, mBlock);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "Cannot copy block data: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::ReleaseBlock()
{
    if (mBlock.data() != nullptr)
    {
        chip::Platform::MemoryFree(mBlock.data());
    }

    mBlock = MutableByteSpan();
    return CHIP_NO_ERROR;
}

} // namespace chip
